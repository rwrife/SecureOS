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
 *       * A dirty snapshot flushes back with os_fs_write_file(path, ...,
 *         append=0) on close() and on explicit internal flushes. Because
 *         the v0 fs bridge marshals content as a NUL-terminated string,
 *         fd-layer files are treated as text: payloads with embedded
 *         NUL bytes are truncated at the first NUL on flush (documented
 *         limitation until a byte-length write ABI lands).
 *   - unlink is implemented as a deterministic truncate-to-empty shim
 *     (`os_fs_write_file(path, "", append=0)`) after an existence check,
 *     so TinyCC cleanup paths can proceed without waiting for a dedicated
 *     delete syscall in the exported user ABI.
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

#define CLIB_POSIX_FD_FIRST 3
#define CLIB_POSIX_FD_SLOTS 16
#define CLIB_POSIX_FD_PATH_CAP 256
#define CLIB_POSIX_FD_FILE_CAP (64u * 1024u)

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
  clib_mem_zero(slot->data, sizeof(slot->data));

  os_status_t st = os_fs_read_file(path, (char *)slot->data,
                                   (unsigned int)sizeof(slot->data));
  if (st != OS_STATUS_OK) {
    errno = status_to_errno(st);
    return -1;
  }

  size_t len = clib_strnlen_local((const char *)slot->data, sizeof(slot->data));
  if (len >= sizeof(slot->data)) {
    errno = EOVERFLOW;
    return -1;
  }

  slot->len = len;
  slot->cursor = 0;
  return 0;
}

/*
 * Persist a dirty snapshot back through os_fs_write_file(). The v0 fs
 * bridge (`app_native_fs_write_file` -> `fs_write_file`) marshals content
 * as a NUL-terminated C string, so the flush stops at the first embedded
 * NUL and `slot->len` shrinks to match what storage actually holds. This
 * keeps fd-layer and storage views consistent for text files (the TinyCC
 * use case) without pretending byte-exact binary support.
 */
static int flush_snapshot(clib_posix_fd_slot_t *slot) {
  if (!slot->dirty) {
    return 0;
  }

  os_status_t st = os_fs_write_file(slot->path, (const char *)slot->data, 0);
  if (st != OS_STATUS_OK) {
    errno = status_to_errno(st);
    return -1;
  }

  slot->len = clib_strnlen_local((const char *)slot->data, slot->len);
  if (slot->cursor > slot->len) {
    slot->cursor = slot->len;
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
  clib_posix_fd_slot_t *slot = fd_slot_from_public(fd);
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
  clib_posix_fd_slot_t *slot = fd_slot_from_public(fd);
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
  clib_posix_fd_slot_t *slot = fd_slot_from_public(fd);
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
  clib_posix_fd_slot_t *slot = fd_slot_from_public(fd);
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
