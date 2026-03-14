#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_table.h"
#include "../kernel/drivers/disk/ramdisk.h"
#include "../kernel/fs/fs_service.h"
#include "../kernel/hal/storage_hal.h"
#include "../kernel/user/app_runtime.h"

#define TEST_SUBJECT_ID 7u

enum {
  TEST_OUTPUT_MAX = 1024,
};

static char g_output[TEST_OUTPUT_MAX];
static size_t g_output_len = 0u;
static size_t g_auth_count = 0u;

static void fail(const char *reason) {
  printf("TEST:FAIL:app_runtime:%s\n", reason);
  exit(1);
}

static size_t string_len(const char *value) {
  size_t len = 0u;
  while (value[len] != '\0') {
    ++len;
  }
  return len;
}

static int string_contains(const char *haystack, const char *needle) {
  size_t haystack_index = 0u;
  size_t needle_len = string_len(needle);

  if (needle_len == 0u) {
    return 1;
  }

  while (haystack[haystack_index] != '\0') {
    size_t match_index = 0u;
    while (needle[match_index] != '\0' &&
           haystack[haystack_index + match_index] != '\0' &&
           haystack[haystack_index + match_index] == needle[match_index]) {
      ++match_index;
    }
    if (match_index == needle_len) {
      return 1;
    }
    ++haystack_index;
  }

  return 0;
}

static void capture_output(const char *message) {
  size_t index = 0u;

  while (message[index] != '\0' && g_output_len + 1u < TEST_OUTPUT_MAX) {
    g_output[g_output_len++] = message[index++];
  }
  g_output[g_output_len] = '\0';
}

static cap_access_state_t allow_disk_io(const char *operation, const char *path) {
  if (operation == 0 || path == 0) {
    return CAP_ACCESS_DENY;
  }

  ++g_auth_count;
  return CAP_ACCESS_ALLOW;
}

int main(void) {
  app_runtime_context_t context;
  char output[128];
  char app_list[64];
  size_t output_len = 0u;
  size_t app_list_len = 0u;

  printf("TEST:START:app_runtime\n");

  cap_table_init();
  storage_hal_reset_for_tests();
  ramdisk_init();
  fs_service_init();

  (void)cap_table_grant(TEST_SUBJECT_ID, CAP_CONSOLE_WRITE);
  (void)cap_table_grant(TEST_SUBJECT_ID, CAP_DISK_IO_REQUEST);
  (void)cap_table_grant(TEST_SUBJECT_ID, CAP_FS_READ);
  (void)cap_table_grant(TEST_SUBJECT_ID, CAP_FS_WRITE);

  context.subject_id = TEST_SUBJECT_ID;
  context.output = capture_output;
  context.authorize_disk_io = allow_disk_io;

  app_list_len = app_runtime_list(app_list, sizeof(app_list));
  if (app_list_len == 0u || !string_contains(app_list, "filedemo")) {
    fail("app_list_missing_filedemo");
  }
  printf("TEST:PASS:app_runtime_list\n");

  if (app_runtime_run("filedemo", &context) != APP_RUNTIME_OK) {
    fail("run_failed");
  }
  printf("TEST:PASS:app_runtime_run_filedemo\n");

  if (g_auth_count != 4u) {
    fail("auth_count_unexpected");
  }
  printf("TEST:PASS:app_runtime_auth_flow\n");

  if (!string_contains(g_output, "[filedemo] start") || !string_contains(g_output, "[filedemo] done")) {
    fail("output_markers_missing");
  }
  printf("TEST:PASS:app_runtime_output_markers\n");

  if (fs_read_file("appdemo.txt", output, sizeof(output), &output_len) != FS_OK) {
    fail("appdemo_read_failed");
  }
  if (output_len != 16u) {
    fail("appdemo_size_unexpected");
  }
  if (!string_contains(output, "filedemo-updated")) {
    fail("appdemo_content_unexpected");
  }
  printf("TEST:PASS:app_runtime_storage_effect\n");

  return 0;
}
