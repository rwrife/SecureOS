/**
 * @file src/posix_fd.c
 * @brief Freestanding POSIX-style fd wrappers over SecureOS file APIs.
 *
 * Issue #538 (M7-TOOLCHAIN-005): TinyCC currently references a small
 * POSIX fd surface (`open`, `close`, `read`, `lseek`, `unlink`) while
 * SecureOS userland exposes file operations through `os_fs_*` entry
 * points. This TU provides a minimal compatibility bridge so those
 * symbols are present in `libclib.a` and can be linked by toolchain
 * consumers without introducing hosted libc dependencies.
 *
 * Design constraints:
 *   - No kernel headers beyond secureos_api.h, no hosted libc use.
 *   - Deterministic fixed-size fd table (no malloc prerequisite).
 *   - Snapshot / write-back semantics over the os_fs_* surface:
 *       * open() loads the file into an in-memory slot (read snapshot);
 *         O_WRONLY/O_RDWR open the same slot for writes.
 *       * O_CREAT creates a missing file lazily (empty snapshot; the
 *         directory entry materialises on the first flush); O_TRUNC
 *         zeroes the snapshot; O_APPEND forces writes to the end.
 *       * read/lseek operate on that snapshot; write() extends it.
 *       * A dirty snapshot flushes back with
 *         os_fs_write_file_bytes(path, ..., len, append=0) on close()
 *         and on explicit internal flushes. Since DEMO-01 (#765) the
 *         fd layer marshals explicit byte lengths across the binary-safe
 *         fs bridge, so payloads with embedded NUL bytes round-trip
 *         byte-for-byte (the old v0 text-bridge truncation limitation is
 *         retired).
 *   - unlink is implemented as a deterministic truncate-to-empty shim
 *     (`os_fs_write_file(path, "", append=0)`) after an existence check,
 *     so TinyCC cleanup paths can proceed without waiting for a dedicated
 *     delete syscall in the exported user ABI.
 *   - fds 0/1/2 are pre-assigned console descriptors (slice 3):
 *       * write(1|2) forwards to os_console_write() in NUL-terminated
 *         chunks (write-through, no snapshot buffering);
 *       * stdin has no console-read syscall in the v0 bridge, so read(0)
 *         deterministically fails with ENOTSUP;
 *       * lseek on any console fd fails with ESPIPE;
 *       * close on fds 0/1/2 succeeds as a no-op (the descriptors are
 *         never released, matching the usual POSIX-reserved convention);
 *       * open() refuses the reserved console aliases with EBUSY so no
 *         file snapshot can alias the console descriptors.
 *
 * This file is called by any userland binary that links against
 * libclib.a and directly invokes the POSIX fd symbols.
 */

#include "../include/clib/posix_fd.h"
#include "../include/clib/errno.h"

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>

#include "../../../include/secureos_api.h"

/* Reserved console alias paths (slice 3 of #538). open() refuses these so
 * no file snapshot can ever alias the console descriptors; they are the
 * conventional POSIX names TinyCC's stubs may probe. */
#define CLIB_CONSOLE_ALIAS_STDIN "/dev/stdin"
#define CLIB_CONSOLE_ALIAS_STDOUT "/dev/stdout"
#define CLIB_CONSOLE_ALIAS_STDERR "/dev/stderr"

static int path_is_console_alias(const char *path) {
  const char *alias;
  size_t i;

  if (path[0] != '/') {
    return 0;
  }
  for (i = 0; i < 3; ++i) {
    alias = (i == 0)   ? CLIB_CONSOLE_ALIAS_STDIN
            : (i == 1) ? CLIB_CONSOLE_ALIAS_STDOUT
                       : CLIB_CONSOLE_ALIAS_STDERR;
    size_t n = 0;
    while (alias[n] != '\0' && path[n] == alias[n]) {
      ++n;
    }
    if (alias[n] == '\0' && path[n] == '\0') {
      return 1;
    }
  }
  return 0;
}

#define CLIB_POSIX_FD_FIRST 3
#define CLIB_POSIX_FD_SLOTS 16
#define CLIB_POSIX_FD_PATH_CAP 256
#define CLIB_POSIX_FD_FILE_CAP (64u * 1024u)

/* Reserved console descriptors (slice 3 of #538). They are not table
 * slots: fd 0 is a deterministic unsupported-read stub (the v0 bridge
 * has no console-read syscall), fds 1/2 write through to
 * os_console_write() in NUL-terminated chunks. */
#define CLIB_CONSOLE_FD_STDIN 0
#define CLIB_CONSOLE_FD_STDOUT 1
#define CLIB_CONSOLE_FD_STDERR 2
#define CLIB_CONSOLE_CHUNK_CAP 256

static int fd_is_console(int fd) {
  return fd == CLIB_CONSOLE_FD_STDIN || fd == CLIB_CONSOLE_FD_STDOUT ||
         fd == CLIB_CONSOLE_FD_STDERR;
}

/*
 * Forward a byte range to os_console_write() in NUL-terminated chunks.
 * The v0 console syscall marshals a single C string per call, so
 * embedded NUL bytes act as chunk boundaries (documented limitation of
 * the console path only — the fs snapshot path is byte-safe since
 * #765): each maximal run of non-NUL bytes becomes one
 * `os_console_write` call and empty runs
 * between consecutive NULs emit no call at all. The console has no
 * short-write semantics: every chunk either succeeds or the write fails
 * wholesale with EIO.
 */
static ssize_t console_write_bytes(int fd, const void *buf, size_t count) {
  const unsigned char *p = (const unsigned char *)buf;
  char chunk[CLIB_CONSOLE_CHUNK_CAP + 1];
  size_t done = 0;

  (void)fd; /* stdout and stderr share the single console sink. */

  while (done < count) {
    size_t n = 0;
    while (n < CLIB_CONSOLE_CHUNK_CAP && (done + n) < count &&
           p[done + n] != '\0') {
      chunk[n] = (char)p[done + n];
      ++n;
    }
    if (n > 0) {
      chunk[n] = '\0';
      if (os_console_write(chunk) != OS_STATUS_OK) {
        errno = EIO;
        return -1;
      }
      /* Skip the terminating NUL byte itself (or advance by the full
       * chunk when the run hit the chunk cap or end of payload). */
      done += (n < CLIB_CONSOLE_CHUNK_CAP && (done + n) < count) ? n + 1 : n;
    } else {
      /* Payload starts with a NUL at this offset: boundary consumed,
       * no empty console write emitted. */
      done += 1;
    }
  }

  return (ssize_t)count;
}

typedef struct clib_posix_fd_slot {
  int in_use;
  int writable; /* fd opened with O_WRONLY/O_RDWR: write() permitted. */
  int append;   /* O_APPEND: each write() first seeks to end. */
  int dirty;    /* snapshot diverged from storage; flush pending. */
  char path[CLIB_POSIX_FD_PATH_CAP];
  unsigned char data[CLIB_POSIX_FD_FILE_CAP];
  size_t len;
  size_t cursor;
} clib_posix_fd_slot_t;

static clib_posix_fd_slot_t g_slots[CLIB_POSIX_FD_SLOTS];

static void clib_mem_zero(void *dst, size_t n) {
  unsigned char *p = (unsigned char *)dst;
  for (size_t i = 0; i < n; ++i) {
    p[i] = 0;
  }
}

static void clib_mem_copy(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  for (size_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }
}

static size_t clib_strnlen_local(const char *s, size_t cap) {
  size_t n = 0;
  while (n < cap && s[n] != '\0') {
    ++n;
  }
  return n;
}

static int path_copy(char dst[CLIB_POSIX_FD_PATH_CAP], const char *src) {
  size_t n = clib_strnlen_local(src, CLIB_POSIX_FD_PATH_CAP);
  if (n == 0 || n >= CLIB_POSIX_FD_PATH_CAP) {
    errno = EINVAL;
    return -1;
  }
  for (size_t i = 0; i < n; ++i) {
    dst[i] = src[i];
  }
  dst[n] = '\0';
  return 0;
}

static clib_posix_fd_slot_t *fd_slot_from_public(int fd) {
  int idx = fd - CLIB_POSIX_FD_FIRST;
  if (idx < 0 || idx >= CLIB_POSIX_FD_SLOTS) {
    return NULL;
  }
  if (!g_slots[idx].in_use) {
    return NULL;
  }
  return &g_slots[idx];
}

static int status_to_errno(os_status_t status) {
  switch (status) {
    case OS_STATUS_OK:
      return 0;
    case OS_STATUS_DENIED:
      return EACCES;
    case OS_STATUS_NOT_FOUND:
      return ENOENT;
    case OS_STATUS_ERROR:
      return EIO;
    default:
      return EIO;
  }
}

static int load_snapshot(clib_posix_fd_slot_t *slot, const char *path) {
  unsigned int byte_len = 0u;

  clib_mem_zero(slot->data, sizeof(slot->data));

  /* DEMO-01 (#765): read through the binary-safe byte API so embedded
   * NUL payloads survive intact and the snapshot length is the actual
   * byte count (never a strnlen inference). */
  os_status_t st = os_fs_read_file_bytes(path, slot->data,
                                         (unsigned int)sizeof(slot->data),
                                         &byte_len);
  if (st != OS_STATUS_OK) {
    errno = status_to_errno(st);
    return -1;
  }

  if (byte_len > sizeof(slot->data)) {
    errno = EOVERFLOW;
    return -1;
  }

  slot->len = byte_len;
  slot->cursor = 0;
  return 0;
}

/*
 * Persist a dirty snapshot back through os_fs_write_file_bytes().
 * DEMO-01 (#765): the byte-length write path marshals exactly
 * `slot->len` bytes, so payloads with embedded / leading / trailing NUL
 * bytes round-trip byte-for-byte (the v0 text bridge's C-string
 * truncation no longer applies to the fd layer).
 */
static int flush_snapshot(clib_posix_fd_slot_t *slot) {
  if (!slot->dirty) {
    return 0;
  }

  os_status_t st = os_fs_write_file_bytes(slot->path, slot->data,
                                          (unsigned int)slot->len, 0);
  if (st != OS_STATUS_OK) {
    errno = status_to_errno(st);
    return -1;
  }

  slot->dirty = 0;
  return 0;
}

int open(const char *path, int flags, ...) {
  int access;
  int want_create;
  int idx = -1;
  clib_posix_fd_slot_t *slot;

  /* The mode argument is only meaningful when O_CREAT is set; the v0 fs
   * bridge has no permission-bit surface, so its value is consumed here
   * purely for call-shape compatibility and ignored otherwise. */
  if ((flags & O_CREAT) != 0) {
    va_list ap;
    va_start(ap, flags);
    (void)va_arg(ap, int);
    va_end(ap);
  }

  if (!path || path[0] == '\0') {
    errno = EINVAL;
    return -1;
  }

  if (path_is_console_alias(path)) {
    /* Reserved console streams: refuse file-style opens so fds 0/1/2
     * semantics stay exclusive to the console branch. */
    errno = EBUSY;
    return -1;
  }

  access = flags & O_ACCMODE;
  want_create = (flags & O_CREAT) != 0;

  for (int i = 0; i < CLIB_POSIX_FD_SLOTS; ++i) {
    if (!g_slots[i].in_use) {
      idx = i;
      break;
    }
  }
  if (idx < 0) {
    errno = EMFILE;
    return -1;
  }

  slot = &g_slots[idx];
  clib_mem_zero(slot, sizeof(*slot));
  if (path_copy(slot->path, path) != 0) {
    return -1;
  }

  if (load_snapshot(slot, path) != 0) {
    int load_errno = errno;
    if (!(load_errno == ENOENT && want_create)) {
      return -1;
    }
    /* O_CREAT on a missing path: start from an empty snapshot. The
     * directory entry materialises lazily on the first flush, matching
     * os_fs_write_file()'s create-if-absent behavior. */
    slot->len = 0;
    slot->cursor = 0;
    slot->dirty = 1;
  } else if ((flags & O_TRUNC) != 0 && slot->len != 0) {
    clib_mem_zero(slot->data, sizeof(slot->data));
    slot->len = 0;
    slot->cursor = 0;
    slot->dirty = 1;
  }

  slot->writable = (access != O_RDONLY);
  slot->append = (flags & O_APPEND) != 0;
  slot->in_use = 1;
  return CLIB_POSIX_FD_FIRST + idx;
}

int close(int fd) {
  clib_posix_fd_slot_t *slot;

  if (fd_is_console(fd)) {
    /* Reserved descriptors: close succeeds as a no-op; the console
     * streams are never released. */
    return 0;
  }

  slot = fd_slot_from_public(fd);
  if (!slot) {
    errno = EBADF;
    return -1;
  }

  if (flush_snapshot(slot) != 0) {
    /* Keep the slot open so the caller can retry/close explicitly; errno
     * already carries the storage failure mapping. */
    return -1;
  }

  clib_mem_zero(slot, sizeof(*slot));
  return 0;
}

ssize_t read(int fd, void *buf, size_t count) {
  clib_posix_fd_slot_t *slot;

  if (fd == CLIB_CONSOLE_FD_STDIN) {
    /* The v0 bridge exposes no console-read syscall, so stdin is a
     * deterministic unsupported stream rather than a silent EOF (an EOF
     * would let read loops terminate with bogus success). */
    errno = ENOTSUP;
    return -1;
  }
  if (fd_is_console(fd)) {
    errno = EBADF; /* stdout/stderr are write-side streams. */
    return -1;
  }

  slot = fd_slot_from_public(fd);
  if (!slot) {
    errno = EBADF;
    return -1;
  }
  if (!buf && count > 0) {
    errno = EFAULT;
    return -1;
  }
  if (count == 0) {
    return 0;
  }

  if (slot->cursor >= slot->len) {
    return 0;
  }

  size_t avail = slot->len - slot->cursor;
  size_t take = (count < avail) ? count : avail;

  if (take > (size_t)LONG_MAX) {
    errno = EOVERFLOW;
    return -1;
  }

  clib_mem_copy(buf, slot->data + slot->cursor, take);
  slot->cursor += take;
  return (ssize_t)take;
}

ssize_t write(int fd, const void *buf, size_t count) {
  clib_posix_fd_slot_t *slot;

  if (fd_is_console(fd)) {
    if (fd == CLIB_CONSOLE_FD_STDIN) {
      errno = EBADF; /* stdin is a read-side stream. */
      return -1;
    }
    if (!buf && count > 0) {
      errno = EFAULT;
      return -1;
    }
    if (count == 0) {
      return 0;
    }
    return console_write_bytes(fd, buf, count);
  }

  slot = fd_slot_from_public(fd);
  if (!slot) {
    errno = EBADF;
    return -1;
  }
  if (!slot->writable) {
    errno = EBADF; /* read-only fd: canonical POSIX write rejection. */
    return -1;
  }
  if (!buf && count > 0) {
    errno = EFAULT;
    return -1;
  }
  if (count == 0) {
    return 0;
  }

  if (slot->append) {
    slot->cursor = slot->len;
  }

  if (count > CLIB_POSIX_FD_FILE_CAP ||
      slot->cursor > CLIB_POSIX_FD_FILE_CAP - count) {
    errno = ENOSPC;
    return -1;
  }

  clib_mem_copy(slot->data + slot->cursor, buf, count);
  slot->cursor += count;
  if (slot->cursor > slot->len) {
    slot->len = slot->cursor;
  }
  slot->dirty = 1;
  return (ssize_t)count;
}

off_t lseek(int fd, off_t offset, int whence) {
  clib_posix_fd_slot_t *slot;

  if (fd_is_console(fd)) {
    errno = ESPIPE; /* console streams are not seekable. */
    return (off_t)-1;
  }

  slot = fd_slot_from_public(fd);
  if (!slot) {
    errno = EBADF;
    return (off_t)-1;
  }

  long long base = 0;
  switch (whence) {
    case SEEK_SET:
      base = 0;
      break;
    case SEEK_CUR:
      base = (long long)slot->cursor;
      break;
    case SEEK_END:
      base = (long long)slot->len;
      break;
    default:
      errno = EINVAL;
      return (off_t)-1;
  }

  long long next = base + (long long)offset;
  if (next < 0) {
    errno = EINVAL;
    return (off_t)-1;
  }
  if ((unsigned long long)next > (unsigned long long)(~(size_t)0) ||
      next > (long long)LONG_MAX) {
    errno = EOVERFLOW;
    return (off_t)-1;
  }

  slot->cursor = (size_t)next;
  return (off_t)slot->cursor;
}

int unlink(const char *path) {
  os_status_t st;
  int probe_fd;

  if (!path || path[0] == '\0') {
    errno = EINVAL;
    return -1;
  }

  /* Existence check first so missing paths still surface ENOENT instead of
   * creating a brand-new empty file via os_fs_write_file(). */
  probe_fd = open(path, O_RDONLY);
  if (probe_fd < 0) {
    return -1;
  }
  (void)close(probe_fd);

  /* v0 unlink shim: SecureOS does not yet expose a dedicated delete syscall
   * in the public user ABI, so model unlink as truncate-to-empty. This is
   * enough for TinyCC temporary-file cleanup paths that only require the path
   * to become non-material for subsequent reads. */
  st = os_fs_write_file(path, "", 0);
  if (st != OS_STATUS_OK) {
    errno = status_to_errno(st);
    return -1;
  }

  return 0;
}
