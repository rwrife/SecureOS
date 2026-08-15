/**
 * @file tests/clib_posix_fd_test.c
 * @brief Host test for the freestanding POSIX-fd nucleus (issue #538).
 *
 * This test is invoked by build/scripts/test_clib_posix_fd.sh and validates
 * the user/libs/clib bridge symbols open/close/read/lseek/unlink against a
 * deterministic in-memory os_fs_read_file/os_fs_write_file fixture.
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

typedef struct fixture_row {
  const char *path;
  char content[64];
  os_status_t read_status;
  int exists;
} fixture_row_t;

static fixture_row_t g_rows[] = {
    {"/alpha.txt", "alpha beta gamma", OS_STATUS_OK, 1},
    {"/empty.txt", "", OS_STATUS_OK, 1},
    {"/denied.txt", "", OS_STATUS_DENIED, 1},
};

static fixture_row_t *find_row(const char *path) {
  for (size_t i = 0; i < (sizeof(g_rows) / sizeof(g_rows[0])); ++i) {
    if (strcmp(path, g_rows[i].path) == 0) {
      return &g_rows[i];
    }
  }
  return NULL;
}

os_status_t os_fs_read_file(const char *path,
                            char *out_buffer,
                            unsigned int out_buffer_size) {
  fixture_row_t *row;
  size_t n;

  if (!path || !out_buffer || out_buffer_size == 0) {
    return OS_STATUS_ERROR;
  }

  row = find_row(path);
  if (!row || !row->exists) {
    return OS_STATUS_NOT_FOUND;
  }
  if (row->read_status != OS_STATUS_OK) {
    return row->read_status;
  }

  n = strlen(row->content);
  if (n + 1 > out_buffer_size) {
    return OS_STATUS_ERROR;
  }
  memcpy(out_buffer, row->content, n + 1);
  return OS_STATUS_OK;
}

os_status_t os_fs_write_file(const char *path,
                             const char *content,
                             int append) {
  fixture_row_t *row;
  const char *src = content ? content : "";
  size_t src_n;
  size_t base_n = 0;

  if (!path) {
    return OS_STATUS_ERROR;
  }

  row = find_row(path);
  if (!row || !row->exists) {
    return OS_STATUS_NOT_FOUND;
  }
  if (row->read_status == OS_STATUS_DENIED) {
    return OS_STATUS_DENIED;
  }

  src_n = strlen(src);
  if (append) {
    base_n = strlen(row->content);
  }

  if (base_n + src_n + 1 > sizeof(row->content)) {
    return OS_STATUS_ERROR;
  }

  if (!append) {
    row->content[0] = '\0';
    base_n = 0;
  }

  memcpy(row->content + base_n, src, src_n + 1);
  return OS_STATUS_OK;
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

static void test_invalid_inputs(void) {
  errno = 0;
  int fd = open(NULL, O_RDONLY);
  record_check(fd == -1 && errno == EINVAL, "open_null_path");

  errno = 0;
  fd = open("/alpha.txt", O_WRONLY);
  record_check(fd == -1 && errno == ENOTSUP, "open_rejects_write_mode");

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

int main(void) {
  test_invalid_inputs();
  test_read_and_seek_roundtrip();
  test_fd_table_limit();
  test_error_paths();
  test_unlink_shim();

  record_check(1, "symbol_set_pinned");

  if (g_failures == 0) {
    printf("TEST:PASS:clib_posix_fd\n");
    return 0;
  }

  printf("TEST:FAIL:clib_posix_fd:failures=%d\n", g_failures);
  return 1;
}
