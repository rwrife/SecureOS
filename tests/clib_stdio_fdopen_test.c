/**
 * @file tests/clib_stdio_fdopen_test.c
 * @brief Host gate for the stdio<->posix_fd fdopen adoption path
 *        (issue #766, TinyCC tccelf.c link-output contract).
 *
 * TinyCC's tccelf.c writes the compiled output as:
 *     fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
 *     fp = fdopen(fd, "wb");   fwrite(...);   fclose(fp);
 *
 * clib's stdio FILE pool is snapshot/backend based while the fd layer
 * keeps its own dirty snapshot per slot. This gate links the REAL
 * shipping shape (stdio.c + posix_fd.c + errno.c) against a
 * deterministic in-memory os_fs_* fixture so the weak-forwarder wiring
 * (clib_stdio_fd_path_fn / _forget_fn / _close_fn) resolves strongly
 * and pins the adoption contract chosen for the in-OS toolchain (see
 * include/clib/stdio.h):
 *
 *   - fdopen("w"/"wb"): the FILE owns the descriptor (fclose closes
 *     it), persistence flows ONLY through the stdio backend, and the
 *     fd's own dirty snapshot is forgotten at adoption so a later
 *     close(fd) can never truncate what the FILE wrote.
 *   - fdopen("r"/"rb"): the content is re-snapshotted through the
 *     read_file backend; fclose still owns/closes the descriptor.
 *   - clib_posix_fd_path / clib_posix_fd_forget behave on live,
 *     console, and invalid fds.
 *
 * Like tests/clib_stdio_test.c, this harness deliberately avoids host
 * <stdio.h>: linking clib's stdio.c replaces the `stdout` data symbol
 * (which glibc stdio internals would then trip over). clib/posix_fd.h
 * supplies open/close/read/write/lseek. All clib stdio surface the test
 * calls is declared by hand with __asm__ name bindings (also a
 * signature drift guard), and TEST: markers go out through a raw
 * SYS_write syscall so neither clib's write(1,...) (fixture no-op) nor
 * host stdio is involved.
 *
 * This test is invoked by build/scripts/test_clib_stdio_fdopen.sh.
 */

#include <stddef.h>
#include <sys/syscall.h>

#include "clib/errno.h"
#include "clib/posix_fd.h"
#include "secureos_api.h"

extern long syscall(long number, ...);

/* ---- by-hand clib stdio surface (same shape as clib_stdio_test.c) ------- */

typedef enum {
  CLIB_STDIO_OK     = 0,
  CLIB_STDIO_DENIED = 1,
  CLIB_STDIO_ERROR  = 2
} clib_stdio_status_t;

typedef struct clib_stdio_backend {
  clib_stdio_status_t (*read_file)(const char *path, char *buf,
                                   size_t *io_size, void *ctx);
  clib_stdio_status_t (*write_file)(const char *path, const char *content,
                                    size_t size, int append, void *ctx);
  clib_stdio_status_t (*console_write)(const char *message, void *ctx);
  void *ctx;
} clib_stdio_backend_t;

struct clib_FILE;
typedef struct clib_FILE clib_FILE_t;

void clib_stdio_init(const clib_stdio_backend_t *backend);
void clib_stdio_shutdown(void);

clib_FILE_t *fdopen(int fd, const char *mode) __asm__("fdopen");
size_t fwrite(const void *buf, size_t size, size_t nmemb, clib_FILE_t *fp)
    __asm__("fwrite");
size_t fread(void *buf, size_t size, size_t nmemb, clib_FILE_t *fp)
    __asm__("fread");
int fclose(clib_FILE_t *fp) __asm__("fclose");

/* ---- marker emit: raw syscall, never clib write / host stdio ----------- */

static size_t raw_len(const char *s) {
  size_t n = 0;
  while (s[n] != '\0') ++n;
  return n;
}

static int raw_streq(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (*a != *b) return 0;
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

static void emit_line(const char *line) {
  (void)syscall(SYS_write, 1, line, raw_len(line));
  (void)syscall(SYS_write, 1, "\n", 1);
}

static int g_failures = 0;

static void record_check(int ok, const char *name) {
  char marker[128];
  size_t o = 0;
  const char *prefix = ok ? "TEST:PASS:clib_stdio_fdopen:"
                          : "TEST:FAIL:clib_stdio_fdopen:";
  if (!ok) g_failures++;
  while (*prefix != '\0' && o < sizeof marker - 2) marker[o++] = *prefix++;
  while (*name != '\0' && o < sizeof marker - 2) marker[o++] = *name++;
  marker[o] = '\0';
  emit_line(marker);
}

/* ---- fixture: row store behind both layers ------------------------------ */

#define FIXTURE_MAX_ROWS 8
#define FIXTURE_ROW_CAP 256

typedef struct fixture_row {
  char path[32];
  char content[FIXTURE_ROW_CAP];
  size_t content_len;
  int exists;
} fixture_row_t;

static fixture_row_t g_rows[FIXTURE_MAX_ROWS] = {
    {"/out.bin", {0}, 0, 1},
    {"/alpha.txt", {'a','l','p','h','a',' ','b','e','t','a',' ','g','a','m','m','a'}, 16, 1},
};
static size_t g_row_count = 2;

/* Reset the row store to the pristine two-row state. The fd table in
 * posix_fd.c has no reset hook, but every test closes (or transfers
 * via fclose) the descriptors it opens, so per-test rows are enough. */
static const char k_alpha_initial[16] = {'a','l','p','h','a',' ','b','e','t','a',' ','g','a','m','m','a'};

static void fixture_reset(void) {
  size_t i;
  g_rows[0].content_len = 0;
  g_rows[1].content_len = 16;
  for (i = 0; i < 16; ++i) g_rows[1].content[i] = k_alpha_initial[i];
  g_row_count = 2;
}
static fixture_row_t *find_row(const char *path) {
  size_t i;
  for (i = 0; i < g_row_count; ++i) {
    if (raw_streq(path, g_rows[i].path)) {
      return &g_rows[i];
    }
  }
  return NULL;
}

static fixture_row_t *ensure_row(const char *path) {
  fixture_row_t *row = find_row(path);
  size_t i = 0;
  if (row != NULL) {
    return row;
  }
  if (g_row_count >= FIXTURE_MAX_ROWS) {
    return NULL;
  }
  row = &g_rows[g_row_count++];
  while (path[i] != '\0' && i < sizeof row->path - 1) {
    row->path[i] = path[i];
    ++i;
  }
  row->path[i] = '\0';
  row->content_len = 0;
  row->exists = 1;
  return row;
}

os_status_t os_fs_read_file_bytes(const char *path, void *out_buffer,
                                  unsigned int out_buffer_size,
                                  unsigned int *out_len) {
  fixture_row_t *row;
  size_t i;
  if (path == NULL || out_buffer == NULL || out_buffer_size == 0 ||
      out_len == NULL) {
    return OS_STATUS_ERROR;
  }
  row = find_row(path);
  if (row == NULL || !row->exists) {
    return OS_STATUS_NOT_FOUND;
  }
  if (row->content_len > out_buffer_size) {
    return OS_STATUS_ERROR;
  }
  for (i = 0; i < row->content_len; ++i) {
    ((char *)out_buffer)[i] = row->content[i];
  }
  *out_len = (unsigned int)row->content_len;
  return OS_STATUS_OK;
}

os_status_t os_fs_write_file_bytes(const char *path, const void *content,
                                   unsigned int content_len, int append) {
  fixture_row_t *row;
  size_t base_n = 0;
  size_t i;
  if (path == NULL || content == NULL) {
    return OS_STATUS_ERROR;
  }
  row = ensure_row(path);
  if (row == NULL) {
    return OS_STATUS_ERROR;
  }
  if (append) {
    base_n = row->content_len;
  }
  if (base_n + content_len > FIXTURE_ROW_CAP) {
    return OS_STATUS_ERROR;
  }
  for (i = 0; i < content_len; ++i) {
    row->content[base_n + i] = ((const char *)content)[i];
  }
  row->content_len = base_n + content_len;
  return OS_STATUS_OK;
}

os_status_t os_fs_read_file(const char *path, char *out_buffer,
                            unsigned int out_buffer_size) {
  unsigned int len = 0;
  return os_fs_read_file_bytes(path, out_buffer, out_buffer_size, &len);
}

os_status_t os_fs_write_file(const char *path, const char *content,
                             int append) {
  return os_fs_write_file_bytes(path, content,
                                (unsigned int)raw_len(content), append);
}

os_status_t os_console_write(const char *message) {
  (void)message;
  return OS_STATUS_OK;
}

/* ---- stdio backend wired straight onto the fixture ---------------------- */

static clib_stdio_status_t backend_read(const char *path, char *buf,
                                        size_t *io_size, void *ctx) {
  unsigned int len = (unsigned int)*io_size;
  os_status_t st;
  (void)ctx;
  st = os_fs_read_file_bytes(path, buf, len, &len);
  if (st != OS_STATUS_OK) {
    return CLIB_STDIO_ERROR;
  }
  *io_size = len;
  return CLIB_STDIO_OK;
}

static clib_stdio_status_t backend_write(const char *path,
                                         const char *content, size_t size,
                                         int append, void *ctx) {
  os_status_t st;
  (void)ctx;
  st = os_fs_write_file_bytes(path, content, (unsigned int)size, append);
  return (st == OS_STATUS_OK) ? CLIB_STDIO_OK : CLIB_STDIO_ERROR;
}

static clib_stdio_status_t backend_console(const char *message, void *ctx) {
  (void)message;
  (void)ctx;
  return CLIB_STDIO_OK;
}

static int row_matches(const char *path, const void *payload, size_t n) {
  fixture_row_t *row = find_row(path);
  size_t i;
  if (row == NULL || row->content_len != n) {
    return 0;
  }
  for (i = 0; i < n; ++i) {
    if (row->content[i] != ((const char *)payload)[i]) {
      return 0;
    }
  }
  return 1;
}

/* TinyCC's exact sequence: open -> fdopen("wb") -> fwrite -> fclose.
 * The row must end up with exactly the payload, the descriptor must be
 * closed by fclose (ownership transfer), and no second write-back may
 * have truncated or duplicated it. */
static void test_fdopen_w_ownership_transfer(void) {
  fixture_reset();
  static const char payload[6] = {'B', 'I', 'N', 0, 'X', 'Y'};
  char probe[1];
  int fd = open("/out.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  clib_FILE_t *fp;
  record_check(fd >= 3, "tinycc_open_creat");
  if (fd < 3) {
    return;
  }
  fp = fdopen(fd, "wb");
  record_check(fp != NULL, "fdopen_wb_nonnull");
  if (fp == NULL) {
    return;
  }
  record_check(fwrite(payload, 1, sizeof payload, fp) == sizeof payload,
               "fwrite_full_count");
  record_check(fclose(fp) == 0, "fclose_ok");

  record_check(row_matches("/out.bin", payload, sizeof payload),
               "payload_persisted_exact");
  errno = 0;
  record_check(read(fd, probe, 1) == -1 && errno == EBADF,
               "fclose_closed_adopted_fd");
}

/* A dirty fd whose content is adopted + forgotten must never flush its
 * stale snapshot over what the FILE wrote (the truncate hazard). */
static void test_fdopen_w_forgets_dirty_fd_snapshot(void) {
  fixture_reset();
  int fd = open("/alpha.txt", O_RDWR, 0);
  clib_FILE_t *fp;
  static const char payload[5] = {'F', 'R', 'E', 'S', 'H'};
  record_check(fd >= 3, "alpha_open_rdwr");
  if (fd < 3) {
    return;
  }
  record_check(write(fd, "XX", 2) == 2, "alpha_fd_write_dirty");

  fp = fdopen(fd, "wb");
  record_check(fp != NULL, "fdopen_after_dirty_write");
  if (fp == NULL) {
    close(fd);
    return;
  }
  fwrite(payload, 1, sizeof payload, fp);
  record_check(fclose(fp) == 0, "fclose_forget_case");

  record_check(row_matches("/alpha.txt", payload, sizeof payload),
               "stale_fd_snapshot_never_flushed");
}

/* fdopen("r"): re-snapshot through the backend; fclose owns/closes. */
static void test_fdopen_r_reads_snapshot(void) {
  fixture_reset();
  int fd = open("/alpha.txt", O_RDONLY, 0);
  clib_FILE_t *fp;
  char buf[32];
  size_t i;
  for (i = 0; i < sizeof buf; ++i) buf[i] = 0;
  record_check(fd >= 3, "alpha_open_rdonly");
  if (fd < 3) {
    return;
  }
  fp = fdopen(fd, "r");
  record_check(fp != NULL, "fdopen_r_nonnull");
  if (fp == NULL) {
    close(fd);
    return;
  }
  record_check(fread(buf, 1, sizeof buf, fp) == 16 &&
                   raw_streq(buf, "alpha beta gamma"),
               "fread_full_snapshot");
  record_check(fclose(fp) == 0, "fclose_r_case");
  errno = 0;
  record_check(close(fd) == -1 && errno == EBADF, "fd_closed_by_fclose_r");
}

/* Failure arms return NULL and leave the descriptor untouched. */
static void test_fdopen_invalid_inputs(void) {
  fixture_reset();
  int fd = open("/alpha.txt", O_RDONLY, 0);
  record_check(fd >= 3, "invalid_case_open");
  if (fd < 3) {
    return;
  }
  record_check(fdopen(fd, "r+") == NULL, "mode_rplus_rejected");
  record_check(fdopen(fd, "") == NULL, "mode_empty_rejected");
  record_check(fdopen(fd, NULL) == NULL, "mode_null_rejected");
  record_check(fdopen(9999, "rb") == NULL, "bad_fd_rejected");
  /* Console fds are not file slots: adoption must refuse them. */
  record_check(fdopen(1, "w") == NULL, "console_fd_rejected");
  record_check(close(fd) == 0, "invalid_case_close_still_open");
}

/* posix_fd hooks used directly (they are public clib surface now). */
static void test_posix_fd_hooks(void) {
  fixture_reset();
  const char *p;
  int fd = open("/alpha.txt", O_RDONLY, 0);
  record_check(fd >= 3, "hooks_open");
  if (fd < 3) {
    return;
  }
  p = clib_posix_fd_path(fd);
  record_check(p != NULL && raw_streq(p, "/alpha.txt"), "path_live_fd");
  record_check(clib_posix_fd_path(9999) == NULL, "path_invalid_fd_null");
  record_check(clib_posix_fd_path(1) == NULL, "path_console_fd_null");

  record_check(clib_posix_fd_forget(fd) == 0, "forget_live_fd");
  errno = 0;
  record_check(clib_posix_fd_forget(9999) == -1 && errno == EBADF,
               "forget_invalid_fd_ebadf");
  record_check(close(fd) == 0, "hooks_close");
  errno = 0;
  record_check(clib_posix_fd_forget(fd) == -1 && errno == EBADF,
               "forget_after_close_ebadf");
  record_check(clib_posix_fd_path(fd) == NULL, "path_after_close_null");
}

/* Forget a DIRTY fd then close it: the pending write-back is dropped. */
static void test_forget_drops_pending_flush(void) {
  fixture_reset();
  int fd = open("/out.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  record_check(fd >= 3, "forget_flush_open");
  if (fd < 3) {
    return;
  }
  record_check(write(fd, "STALE", 5) == 5, "forget_flush_write_dirty");
  record_check(clib_posix_fd_forget(fd) == 0, "forget_dirty_fd_ok");
  record_check(close(fd) == 0, "forget_close_ok");
  record_check(row_matches("/out.bin", "", 0), "forget_close_wrote_nothing");
}

int main(void) {
  static const clib_stdio_backend_t backend = {
      backend_read,
      backend_write,
      backend_console,
      NULL,
  };
  clib_stdio_init(&backend);

  emit_line("TEST:START:clib_stdio_fdopen");
  test_fdopen_w_ownership_transfer();
  test_fdopen_w_forgets_dirty_fd_snapshot();
  test_fdopen_r_reads_snapshot();
  test_fdopen_invalid_inputs();
  test_posix_fd_hooks();
  test_forget_drops_pending_flush();
  record_check(1, "symbol_set_pinned");
  clib_stdio_shutdown();

  if (g_failures == 0) {
    emit_line("TEST:PASS:clib_stdio_fdopen");
    return 0;
  }
  emit_line("TEST:FAIL:clib_stdio_fdopen:failures");
  return 1;
}
