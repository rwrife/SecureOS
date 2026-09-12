/**
 * @file tests/clib_posix_fd_test.c
 * @brief Host test for the freestanding POSIX-fd nucleus (issue #538).
 *
 * This test is invoked by build/scripts/test_clib_posix_fd.sh and validates
 * the user/libs/clib bridge symbols open/close/read/write/lseek/unlink
 * against a deterministic in-memory os_fs_read_file/os_fs_write_file
 * fixture. The fixture models the v0 fs bridge semantics, including
 * create-if-absent on writes (mirroring fs_write_file_bytes()).
 *
 * The slice-3 console-descriptor surface (fds 0/1/2) is validated against
 * an os_console_write recorder fixture that captures every forwarded
 * NUL-terminated chunk.
 */

#include <stdio.h>
#include <string.h>

#include "clib/errno.h"
#include "clib/posix_fd.h"
#include "secureos_api.h"

static int g_failures = 0;

static void record_check(int ok, const char *name) {
  if (ok) {
    printf("TEST:PASS:clib_posix_fd:%s\n", name);
  } else {
    printf("TEST:FAIL:clib_posix_fd:%s\n", name);
    g_failures++;
  }
}
#define CONSOLE_MAX_CALLS 16
#define CONSOLE_CALL_CAP 272 /* CLIB_CONSOLE_CHUNK_CAP + slack */

static char g_console_calls[CONSOLE_MAX_CALLS][CONSOLE_CALL_CAP];
static size_t g_console_call_count = 0;
static os_status_t g_console_status = OS_STATUS_OK;

os_status_t os_console_write(const char *message) {
  if (g_console_status != OS_STATUS_OK) {
    return g_console_status;
  }
  if (!message) {
    return OS_STATUS_ERROR;
  }
  if (g_console_call_count >= CONSOLE_MAX_CALLS) {
    return OS_STATUS_ERROR;
  }
  if (strlen(message) >= CONSOLE_CALL_CAP) {
    return OS_STATUS_ERROR;
  }
  snprintf(g_console_calls[g_console_call_count], CONSOLE_CALL_CAP, "%s",
           message);
  ++g_console_call_count;
  return OS_STATUS_OK;
}

static void console_reset(void) {
  g_console_call_count = 0;
  g_console_status = OS_STATUS_OK;
  memset(g_console_calls, 0, sizeof(g_console_calls));
}

#define FIXTURE_MAX_ROWS 12
#define FIXTURE_ROW_CAP 64

typedef struct fixture_row {
  char path[32];
  char content[FIXTURE_ROW_CAP];
  size_t content_len; /* exact byte length (payloads may contain NUL) */
  os_status_t read_status;
  int exists;
} fixture_row_t;

static fixture_row_t g_rows[FIXTURE_MAX_ROWS] = {
    {"/alpha.txt", "alpha beta gamma", 16, OS_STATUS_OK, 1},
    {"/empty.txt", "", 0, OS_STATUS_OK, 1},
    {"/denied.txt", "", 0, OS_STATUS_DENIED, 1},
    /* Binary payload with a LEADING NUL. */
    {"/lead.bin", {0, 'L', 'X'}, 3, OS_STATUS_OK, 1},
    /* Binary payload with INTERIOR NUL bytes. */
    {"/mid.bin", {'A', 0, 0, 'B', 0, 'C', 0}, 6, OS_STATUS_OK, 1},
    /* Binary payload with a TRAILING NUL. */
    {"/tail.bin", {'Z', 'Y', 0}, 3, OS_STATUS_OK, 1},
};
static size_t g_row_count = 6;

static fixture_row_t *find_row(const char *path) {
  for (size_t i = 0; i < g_row_count; ++i) {
    if (strcmp(path, g_rows[i].path) == 0) {
      return &g_rows[i];
    }
  }
  return NULL;
}

/*
 * Deterministic in-memory fixture for the binary-safe byte APIs
 * (os_fs_read_file_bytes / os_fs_write_file_bytes, DEMO-01 #765).
 * Rows carry explicit byte lengths so payloads with embedded NULs
 * round-trip; the same row store backs the text os_fs_write_file
 * fixture (unlink shim) so the two views stay consistent, mirroring
 * the kernel where fs_write_file is fs_write_file_bytes + strlen.
 */
os_status_t os_fs_read_file_bytes(const char *path, void *out_buffer,
                                  unsigned int out_buffer_size,
                                  unsigned int *out_len) {
  fixture_row_t *row;

  if (!path || !out_buffer || out_buffer_size == 0 || !out_len) {
    return OS_STATUS_ERROR;
  }

  row = find_row(path);
  if (!row || !row->exists) {
    return OS_STATUS_NOT_FOUND;
  }
  if (row->read_status != OS_STATUS_OK) {
    return row->read_status;
  }

  if (row->content_len > out_buffer_size) {
    return OS_STATUS_ERROR;
  }
  memcpy(out_buffer, row->content, row->content_len);
  *out_len = (unsigned int)row->content_len;
  return OS_STATUS_OK;
}

os_status_t os_fs_write_file_bytes(const char *path, const void *content,
                                   unsigned int content_len, int append) {
  fixture_row_t *row;
  size_t base_n = 0;

  if (!path || !content) {
    return OS_STATUS_ERROR;
  }

  row = find_row(path);
  if (!row || !row->exists) {
    /* Mirrors fs_write_file_bytes(): a missing leaf is created lazily. */
    if (row || g_row_count >= FIXTURE_MAX_ROWS) {
      return OS_STATUS_NOT_FOUND;
    }
    row = &g_rows[g_row_count++];
    memset(row, 0, sizeof(*row));
    snprintf(row->path, sizeof(row->path), "%s", path);
    row->read_status = OS_STATUS_OK;
    row->exists = 1;
  }
  if (row->read_status == OS_STATUS_DENIED) {
    return OS_STATUS_DENIED;
  }

  if (append) {
    base_n = row->content_len;
  }

  if (base_n + content_len > sizeof(row->content)) {
    return OS_STATUS_ERROR;
  }

  if (!append) {
    base_n = 0;
  }

  memcpy(row->content + base_n, content, content_len);
  row->content_len = base_n + content_len;
  return OS_STATUS_OK;
}

/* Text-shaped write used by the unlink truncate-to-empty shim: exact
 * bytes = strlen(content), matching fs_write_file -> fs_write_file_bytes. */
os_status_t os_fs_write_file(const char *path,
                             const char *content,
                             int append) {
  const char *src = content ? content : "";
  return os_fs_write_file_bytes(path, src, (unsigned int)strlen(src), append);
}

static int expect_read_eq(int fd, size_t want_count, const char *want) {
  char buf[64];
  memset(buf, 0, sizeof(buf));
  ssize_t got = read(fd, buf, want_count);
  if (got < 0) {
    return 0;
  }
  if ((size_t)got != strlen(want)) {
    return 0;
  }
  return strcmp(buf, want) == 0;
}

/* Byte-exact read comparison for payloads with embedded NULs (the
 * strlen/strcmp-based helper above cannot express these). */
static int expect_read_bytes_eq(int fd, const unsigned char *want,
                                size_t want_n) {
  unsigned char buf[64];
  ssize_t got = read(fd, buf, sizeof(buf));
  if (got < 0 || (size_t)got != want_n) {
    return 0;
  }
  return memcmp(buf, want, want_n) == 0;
}

static const unsigned char k_lead_payload[3] = {0, 'L', 'X'};
static const unsigned char k_mid_payload[6] = {'A', 0, 0, 'B', 0, 'C'};
static const unsigned char k_tail_payload[3] = {'Z', 'Y', 0};
/* Mixed payload: leading, interior, and trailing NUL bytes. */
static const unsigned char k_copy_payload[7] = {0, 'A', 0, 0, 'B', 'C', 0};

/*
 * DEMO-01 (#765): binary payloads with embedded NUL bytes must survive
 * open/read and write/close round-trips byte-for-byte through the fd
 * layer now that snapshots ride the length-bearing byte APIs.
 */
static void test_binary_roundtrip(void) {
  unsigned char buf[16];
  int fd;

  /* Read paths: leading / interior / trailing NUL payloads. */
  fd = open("/lead.bin", O_RDONLY);
  if (fd < 0) {
    record_check(0, "binary_lead_read_exact");
  } else {
    record_check(expect_read_bytes_eq(fd, k_lead_payload, sizeof(k_lead_payload)),
                 "binary_lead_read_exact");
    record_check(close(fd) == 0, "binary_lead_close");
  }

  fd = open("/mid.bin", O_RDONLY);
  if (fd < 0) {
    record_check(0, "binary_mid_read_exact");
  } else {
    record_check(expect_read_bytes_eq(fd, k_mid_payload, sizeof(k_mid_payload)),
                 "binary_mid_read_exact");
    record_check(close(fd) == 0, "binary_mid_close");
  }

  fd = open("/tail.bin", O_RDONLY);
  if (fd < 0) {
    record_check(0, "binary_tail_read_exact");
  } else {
    record_check(expect_read_bytes_eq(fd, k_tail_payload, sizeof(k_tail_payload)),
                 "binary_tail_read_exact");
    record_check(close(fd) == 0, "binary_tail_close");
  }

  /* Write path: create, persist mixed-NUL payload, close (flush),
   * reopen and compare byte-for-byte. */
  errno = 0;
  fd = open("/copy.bin", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  record_check(fd >= 0, "binary_write_open_creat");
  if (fd >= 0) {
    ssize_t n = write(fd, k_copy_payload, sizeof(k_copy_payload));
    record_check(n == (ssize_t)sizeof(k_copy_payload), "binary_write_returns_count");
    record_check(close(fd) == 0, "binary_write_close_flushes");

    fd = open("/copy.bin", O_RDONLY);
    memset(buf, 0xAA, sizeof(buf));
    ssize_t got = read(fd, buf, sizeof(buf));
    record_check(got == (ssize_t)sizeof(k_copy_payload) &&
                     memcmp(buf, k_copy_payload, sizeof(k_copy_payload)) == 0,
                 "binary_readback_byte_exact");
    record_check(close(fd) == 0, "binary_readback_close");
  }

  /* Append mode still lands exact bytes at the end (payload grows to
   * 8 bytes: the 7-byte k_copy_payload plus one trailing NUL). */
  fd = open("/copy.bin", O_WRONLY | O_APPEND);
  record_check(fd >= 0, "binary_append_open");
  if (fd >= 0) {
    static const unsigned char extra = 0;
    record_check(write(fd, &extra, 1) == 1, "binary_append_write_count");
    record_check(close(fd) == 0, "binary_append_close_flushes");

    fd = open("/copy.bin", O_RDONLY);
    memset(buf, 0xAA, sizeof(buf));
    ssize_t got = read(fd, buf, sizeof(buf));
    record_check(got == 8 &&
                     memcmp(buf, k_copy_payload, sizeof(k_copy_payload)) == 0 &&
                     buf[7] == 0,
                 "binary_append_byte_exact");
    record_check(close(fd) == 0, "binary_append_close_after");
  }
}

static void test_invalid_inputs(void) {
  errno = 0;
  int fd = open(NULL, O_RDONLY);
  record_check(fd == -1 && errno == EINVAL, "open_null_path");

  errno = 0;
  fd = open("/missing.txt", O_WRONLY);
  record_check(fd == -1 && errno == ENOENT, "open_write_missing_no_creat_enoent");

  errno = 0;
  fd = open("/denied.txt", O_RDONLY);
  record_check(fd == -1 && errno == EACCES, "open_maps_denied_to_eacces");
}

static void test_read_and_seek_roundtrip(void) {
  int fd = open("/alpha.txt", O_RDONLY);
  if (fd < 0) {
    record_check(0, "open_alpha_success");
    return;
  }
  record_check(1, "open_alpha_success");

  record_check(expect_read_eq(fd, 5, "alpha"), "read_prefix");

  off_t seek1 = lseek(fd, 6, SEEK_SET);
  record_check(seek1 == 6, "lseek_set");
  record_check(expect_read_eq(fd, 4, "beta"), "read_middle");

  off_t seek2 = lseek(fd, -5, SEEK_END);
  record_check(seek2 >= 0, "lseek_end_minus5");
  record_check(expect_read_eq(fd, 5, "gamma"), "read_suffix");

  char eof_buf[4] = {0};
  ssize_t eof_n = read(fd, eof_buf, sizeof(eof_buf));
  record_check(eof_n == 0, "read_eof_returns_zero");

  record_check(close(fd) == 0, "close_valid_fd");
}

static void test_fd_table_limit(void) {
  int fds[64];
  int opened = 0;

  while (opened < (int)(sizeof(fds) / sizeof(fds[0]))) {
    int fd = open("/alpha.txt", O_RDONLY);
    if (fd < 0) {
      break;
    }
    fds[opened++] = fd;
  }

  record_check(opened > 0, "fd_table_opened_some");
  record_check(errno == EMFILE, "fd_table_emfile");

  for (int i = 0; i < opened; ++i) {
    close(fds[i]);
  }
}

static void test_error_paths(void) {
  errno = 0;
  record_check(close(9999) == -1 && errno == EBADF, "close_invalid_fd");

  errno = 0;
  record_check(read(9999, NULL, 1) == -1 && errno == EBADF,
               "read_invalid_fd");

  errno = 0;
  record_check(lseek(9999, 0, SEEK_SET) == (off_t)-1 && errno == EBADF,
               "lseek_invalid_fd");
}

static void test_unlink_shim(void) {
  int fd;
  char probe[4] = {0};

  errno = 0;
  record_check(unlink("/missing.txt") == -1 && errno == ENOENT,
               "unlink_missing_maps_enoent");

  errno = 0;
  record_check(unlink("/denied.txt") == -1 && errno == EACCES,
               "unlink_denied_maps_eacces");

  errno = 0;
  record_check(unlink("/alpha.txt") == 0, "unlink_truncate_success");

  fd = open("/alpha.txt", O_RDONLY);
  if (fd < 0) {
    record_check(0, "open_after_unlink_success");
    return;
  }
  record_check(1, "open_after_unlink_success");

  errno = 0;
  record_check(read(fd, probe, sizeof(probe)) == 0, "read_after_unlink_is_eof");
  record_check(close(fd) == 0, "close_after_unlink_success");
}

static void test_write_modes(void) {
  int fd;
  char buf[32];

  /* O_WRONLY on an existing file is accepted (write-capable fd). */
  errno = 0;
  fd = open("/alpha.txt", O_WRONLY | O_TRUNC);
  record_check(fd >= 0, "open_write_mode_accepted");
  if (fd < 0) {
    return;
  }

  /* write() on a read-only fd is rejected with EBADF. */
  {
    int ro = open("/alpha.txt", O_RDONLY);
    errno = 0;
    record_check(ro >= 0 && write(ro, "x", 1) == -1 && errno == EBADF,
                 "write_readonly_fd_ebadf");
    record_check(close(ro) == 0, "close_readonly_after_write_attempt");
  }

  /* write() argument validation. */
  errno = 0;
  record_check(write(fd, NULL, 4) == -1 && errno == EFAULT, "write_null_efault");
  record_check(write(fd, "", 0) == 0, "write_zero_returns_zero");

  /* O_WRONLY | O_TRUNC discards prior contents; write + close flushes. */
  ssize_t n = write(fd, "fresh", 5);
  record_check(n == 5, "wronly_write_returns_count");
  record_check(close(fd) == 0, "wronly_close_flushes");

  fd = open("/alpha.txt", O_RDONLY);
  memset(buf, 0, sizeof(buf));
  record_check(fd >= 0 && read(fd, buf, sizeof(buf) - 1) == 5 &&
                   strcmp(buf, "fresh") == 0,
               "wronly_trunc_roundtrip");
  record_check(close(fd) == 0, "close_after_trunc_roundtrip");

  /* O_CREAT on a missing path creates lazily via write-back. */
  errno = 0;
  fd = open("/created.txt", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  record_check(fd >= 0, "creat_missing_accepted");
  if (fd >= 0) {
    record_check(write(fd, "hello", 5) == 5, "creat_write_returns_count");
    record_check(close(fd) == 0, "creat_close_flushes");

    fd = open("/created.txt", O_RDONLY);
    memset(buf, 0, sizeof(buf));
    record_check(fd >= 0 && read(fd, buf, sizeof(buf) - 1) == 5 &&
                     strcmp(buf, "hello") == 0,
                 "creat_write_roundtrip");
    record_check(close(fd) == 0, "close_after_creat_roundtrip");
  }

  /* O_APPEND forces every write to the end, even after an explicit seek. */
  fd = open("/created.txt", O_WRONLY | O_APPEND);
  if (fd >= 0) {
    (void)lseek(fd, 0, SEEK_SET);
    record_check(write(fd, "!", 1) == 1, "append_write_returns_count");
    record_check(close(fd) == 0, "append_close_flushes");

    fd = open("/created.txt", O_RDONLY);
    memset(buf, 0, sizeof(buf));
    record_check(fd >= 0 && read(fd, buf, sizeof(buf) - 1) == 6 &&
                     strcmp(buf, "hello!") == 0,
                 "append_forces_end_of_file");
    record_check(close(fd) == 0, "close_after_append_roundtrip");
  }

  /* Writes that would exceed the fixed snapshot slot fail with ENOSPC. */
  fd = open("/big.txt", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  if (fd >= 0) {
    off_t near_end = lseek(fd, (off_t)(64u * 1024u - 4u), SEEK_SET);
    record_check(near_end == (off_t)(64u * 1024u - 4u), "enospc_seek_ok");
    errno = 0;
    record_check(write(fd, "toobig", 6) == -1 && errno == ENOSPC,
                 "enospc_past_slot_capacity");
    record_check(close(fd) == 0, "enospc_close_ok");
  }
}

static void test_console_fds(void) {
  console_reset();

  /* write(1) forwards to the console recorder. */
  errno = 0;
  record_check(write(1, "hello", 5) == 5, "console_stdout_write_count");
  record_check(g_console_call_count == 1 &&
                   strcmp(g_console_calls[0], "hello") == 0,
               "console_stdout_forwards_chunk");

  /* write(2) shares the same console sink. */
  record_check(write(2, "err", 3) == 3, "console_stderr_write_count");
  record_check(g_console_call_count == 2 &&
                   strcmp(g_console_calls[1], "err") == 0,
               "console_stderr_forwards_chunk");

  /* write(0) is rejected: stdin is a read-side stream. */
  errno = 0;
  record_check(write(0, "x", 1) == -1 && errno == EBADF,
               "console_stdin_write_ebadf");

  /* Embedded NULs split into NUL-bounded chunks with no empty calls. */
  console_reset();
  record_check(write(1, "ab\0cd", 5) == 5, "console_nul_write_count");
  record_check(g_console_call_count == 2 &&
                   strcmp(g_console_calls[0], "ab") == 0 &&
                   strcmp(g_console_calls[1], "cd") == 0,
               "console_nul_splits_chunks");

  console_reset();
  record_check(write(1, "\0", 1) == 1, "console_lone_nul_write_count");
  record_check(g_console_call_count == 0, "console_lone_nul_no_call");

  /* Console failure maps to EIO. */
  console_reset();
  g_console_status = OS_STATUS_ERROR;
  errno = 0;
  record_check(write(1, "boom", 4) == -1 && errno == EIO,
               "console_write_failure_eio");
  console_reset();

  /* Argument validation on console fds. */
  errno = 0;
  record_check(write(1, NULL, 4) == -1 && errno == EFAULT,
               "console_write_null_efault");
  record_check(write(1, "", 0) == 0, "console_write_zero_returns_zero");

  /* read(0) is a deterministic unsupported stub (no v0 console read). */
  {
    char in_buf[8];
    errno = 0;
    record_check(read(0, in_buf, sizeof(in_buf)) == -1 && errno == ENOTSUP,
                 "console_stdin_read_enotsup");
    errno = 0;
    record_check(read(1, in_buf, sizeof(in_buf)) == -1 && errno == EBADF,
                 "console_stdout_read_ebadf");
  }

  /* Console streams are not seekable. */
  errno = 0;
  record_check(lseek(1, 0, SEEK_SET) == (off_t)-1 && errno == ESPIPE,
               "console_lseek_espipe");

  /* close on reserved descriptors succeeds and keeps them usable. */
  errno = 0;
  record_check(close(0) == 0 && close(1) == 0 && close(2) == 0,
               "console_close_noop_success");
  console_reset();
  record_check(write(1, "again", 5) == 5 && g_console_call_count == 1 &&
                   strcmp(g_console_calls[0], "again") == 0,
               "console_write_after_close_still_works");

  /* Reserved console alias paths refuse file-style opens. */
  errno = 0;
  record_check(open("/dev/stdin", O_WRONLY | O_CREAT, S_IRUSR) == -1 &&
                   errno == EBUSY,
               "console_alias_open_ebusy");
  errno = 0;
  record_check(open("/dev/stdout", O_WRONLY) == -1 && errno == EBUSY,
               "console_alias_stdout_ebusy");
}

int main(void) {
  test_invalid_inputs();
  test_read_and_seek_roundtrip();
  test_binary_roundtrip();
  test_fd_table_limit();
  test_error_paths();
  test_write_modes();
  test_console_fds();
  test_unlink_shim();

  record_check(1, "symbol_set_pinned");

  if (g_failures == 0) {
    printf("TEST:PASS:clib_posix_fd\n");
    return 0;
  }

  printf("TEST:FAIL:clib_posix_fd:failures=%d\n", g_failures);
  return 1;
}
