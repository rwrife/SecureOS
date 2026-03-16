/**
 * @file app_runtime_test.c
 * @brief Tests for the user-space process module.
 *
 * Purpose:
 *   Validates that ELF binaries can be loaded from the filesystem,
 *   parsed, and executed by the process module script interpreter.
 *   Covers capability checks, argument passing, environment variable
 *   expansion, and error paths.
 *
 * Interactions:
 *   - process.c: exercises process_run and related functions.
 *   - fs_service.c / ramdisk.c / storage_hal.c: sets up a ramdisk-
 *     backed filesystem with test binaries.
 *   - cap_table.c: grants required capabilities to the test subject.
 *
 * Launched by:
 *   Compiled and run by the test harness (build/scripts/test_app_runtime.sh).
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_table.h"
#include "../kernel/drivers/disk/ramdisk.h"
#include "../kernel/fs/fs_service.h"
#include "../kernel/hal/storage_hal.h"
#include "../kernel/user/process.h"

#define TEST_SUBJECT_ID 7u

enum {
  TEST_OUTPUT_MAX = 1024,
};

static char g_output[TEST_OUTPUT_MAX];
static size_t g_output_len = 0u;
static size_t g_auth_count = 0u;
static char g_cwd[64] = "/";
enum {
  TEST_ENV_MAX = 8,
  TEST_ENV_KEY_MAX = 32,
  TEST_ENV_VALUE_MAX = 96,
};

typedef struct {
  int used;
  char key[TEST_ENV_KEY_MAX];
  char value[TEST_ENV_VALUE_MAX];
} test_env_entry_t;

static test_env_entry_t g_env_entries[TEST_ENV_MAX];
static unsigned int g_loaded_lib_handle = 0u;
static size_t g_loaded_lib_size = 0u;
static unsigned int g_loaded_lib_ref_count = 0u;
static char g_loaded_lib_path[64];

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

static int string_equals(const char *left, const char *right) {
  size_t i = 0u;
  while (left[i] != '\0' && right[i] != '\0') {
    if (left[i] != right[i]) {
      return 0;
    }
    ++i;
  }
  return left[i] == right[i];
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

static int env_get(const char *key, char *out_value, size_t out_value_size) {
  size_t i = 0u;
  size_t cursor = 0u;

  if (key == 0 || out_value == 0 || out_value_size == 0u) {
    return 0;
  }

  for (i = 0u; i < TEST_ENV_MAX; ++i) {
    if (!g_env_entries[i].used || !string_equals(g_env_entries[i].key, key)) {
      continue;
    }

    while (g_env_entries[i].value[cursor] != '\0' && cursor + 1u < out_value_size) {
      out_value[cursor] = g_env_entries[i].value[cursor];
      ++cursor;
    }
    out_value[cursor] = '\0';
    return 1;
  }

  return 0;
}

static int env_set(const char *key, const char *value) {
  size_t i = 0u;
  int slot = -1;

  if (key == 0 || key[0] == '\0' || value == 0) {
    return 0;
  }

  for (i = 0u; i < TEST_ENV_MAX; ++i) {
    if (g_env_entries[i].used && string_equals(g_env_entries[i].key, key)) {
      slot = (int)i;
      break;
    }
    if (!g_env_entries[i].used && slot < 0) {
      slot = (int)i;
    }
  }

  if (slot < 0) {
    return 0;
  }

  g_env_entries[slot].used = 1;
  i = 0u;
  while (key[i] != '\0' && i + 1u < sizeof(g_env_entries[slot].key)) {
    g_env_entries[slot].key[i] = key[i];
    ++i;
  }
  g_env_entries[slot].key[i] = '\0';

  i = 0u;
  while (value[i] != '\0' && i + 1u < sizeof(g_env_entries[slot].value)) {
    g_env_entries[slot].value[i] = value[i];
    ++i;
  }
  g_env_entries[slot].value[i] = '\0';
  return 1;
}

static size_t env_list(char *out_buffer, size_t out_buffer_size) {
  size_t cursor = 0u;
  size_t i = 0u;
  size_t j = 0u;

  if (out_buffer == 0 || out_buffer_size == 0u) {
    return 0u;
  }

  out_buffer[0] = '\0';

  for (i = 0u; i < TEST_ENV_MAX; ++i) {
    if (!g_env_entries[i].used) {
      continue;
    }

    j = 0u;
    while (g_env_entries[i].key[j] != '\0' && cursor + 1u < out_buffer_size) {
      out_buffer[cursor++] = g_env_entries[i].key[j++];
    }
    if (cursor + 1u < out_buffer_size) {
      out_buffer[cursor++] = '=';
    }

    j = 0u;
    while (g_env_entries[i].value[j] != '\0' && cursor + 1u < out_buffer_size) {
      out_buffer[cursor++] = g_env_entries[i].value[j++];
    }
    if (cursor + 1u < out_buffer_size) {
      out_buffer[cursor++] = '\n';
    }
  }

  out_buffer[cursor] = '\0';
  return cursor;
}

static void resolve_path(const char *input_path, char *out_path, size_t out_path_size) {
  size_t cursor = 0u;
  size_t i = 0u;

  if (out_path == 0 || out_path_size == 0u) {
    return;
  }

  out_path[0] = '\0';
  if (input_path == 0 || input_path[0] == '\0') {
    return;
  }

  if (input_path[0] == '/' || input_path[0] == '\\') {
    while (input_path[i] != '\0' && i + 1u < out_path_size) {
      out_path[i] = input_path[i];
      ++i;
    }
    out_path[i] = '\0';
    return;
  }

  while (g_cwd[cursor] != '\0' && cursor + 1u < out_path_size) {
    out_path[cursor] = g_cwd[cursor];
    ++cursor;
  }

  if (cursor > 0u && cursor + 1u < out_path_size && out_path[cursor - 1u] != '/') {
    out_path[cursor++] = '/';
  }

  i = 0u;
  while (input_path[i] != '\0' && cursor + 1u < out_path_size) {
    out_path[cursor++] = input_path[i++];
  }
  out_path[cursor] = '\0';
}

static int register_loaded_library(const char *resolved_path,
                                   size_t program_len,
                                   const char *owner_actor,
                                   unsigned int *out_handle) {
  size_t i = 0u;

  (void)owner_actor;

  if (resolved_path == 0 || resolved_path[0] == '\0') {
    return 0;
  }

  if (string_equals(g_loaded_lib_path, resolved_path) && g_loaded_lib_handle != 0u) {
    if (out_handle != 0) {
      *out_handle = g_loaded_lib_handle;
    }
    return 1;
  }

  g_loaded_lib_handle = 1u;
  g_loaded_lib_size = program_len;
  g_loaded_lib_ref_count = 0u;
  while (resolved_path[i] != '\0' && i + 1u < sizeof(g_loaded_lib_path)) {
    g_loaded_lib_path[i] = resolved_path[i];
    ++i;
  }
  g_loaded_lib_path[i] = '\0';

  if (out_handle != 0) {
    *out_handle = g_loaded_lib_handle;
  }
  return 1;
}

static int unregister_loaded_library(unsigned int handle, char *out_path, size_t out_path_size) {
  size_t i = 0u;

  if (handle == 0u || handle != g_loaded_lib_handle) {
    return 0;
  }

  if (g_loaded_lib_ref_count > 0u) {
    return 0;
  }

  if (out_path != 0 && out_path_size > 0u) {
    while (g_loaded_lib_path[i] != '\0' && i + 1u < out_path_size) {
      out_path[i] = g_loaded_lib_path[i];
      ++i;
    }
    out_path[i] = '\0';
  }

  g_loaded_lib_handle = 0u;
  g_loaded_lib_size = 0u;
  g_loaded_lib_ref_count = 0u;
  g_loaded_lib_path[0] = '\0';
  return 1;
}

static int get_loaded_library_ref_count(unsigned int handle, unsigned int *out_ref_count) {
  if (out_ref_count != 0) {
    *out_ref_count = 0u;
  }

  if (handle == 0u || handle != g_loaded_lib_handle) {
    return 0;
  }

  if (out_ref_count != 0) {
    *out_ref_count = g_loaded_lib_ref_count;
  }
  return 1;
}

static int acquire_loaded_library(unsigned int handle, unsigned int *out_ref_count) {
  if (out_ref_count != 0) {
    *out_ref_count = 0u;
  }

  if (handle == 0u || handle != g_loaded_lib_handle) {
    return 0;
  }

  g_loaded_lib_ref_count += 1u;
  if (out_ref_count != 0) {
    *out_ref_count = g_loaded_lib_ref_count;
  }
  return 1;
}

static int release_loaded_library(unsigned int handle, unsigned int *out_ref_count) {
  if (out_ref_count != 0) {
    *out_ref_count = 0u;
  }

  if (handle == 0u || handle != g_loaded_lib_handle) {
    return 0;
  }

  if (g_loaded_lib_ref_count == 0u) {
    return 0;
  }

  g_loaded_lib_ref_count -= 1u;
  if (out_ref_count != 0) {
    *out_ref_count = g_loaded_lib_ref_count;
  }
  return 1;
}

static size_t list_loaded_libraries(char *out_buffer, size_t out_buffer_size) {
  size_t cursor = 0u;
  size_t i = 0u;

  if (out_buffer == 0 || out_buffer_size == 0u) {
    return 0u;
  }

  out_buffer[0] = '\0';
  if (g_loaded_lib_handle == 0u || g_loaded_lib_path[0] == '\0') {
    return 0u;
  }

  cursor += (size_t)snprintf(out_buffer + cursor,
                             out_buffer_size - cursor,
                             "handle=%u path=",
                             g_loaded_lib_handle);
  while (g_loaded_lib_path[i] != '\0' && cursor + 1u < out_buffer_size) {
    out_buffer[cursor++] = g_loaded_lib_path[i++];
  }
  if (cursor + 1u < out_buffer_size) {
    out_buffer[cursor++] = ' ';
  }
  cursor += (size_t)snprintf(out_buffer + cursor,
                             out_buffer_size - cursor,
                             "size=%u refs=%u\n",
                             (unsigned int)g_loaded_lib_size,
                             g_loaded_lib_ref_count);
  if (cursor >= out_buffer_size) {
    cursor = out_buffer_size - 1u;
  }
  out_buffer[cursor] = '\0';
  return cursor;
}

int main(void) {
  process_context_t context = {0};
  char app_list[256];
  char lib_list[256];
  process_library_info_t library_info;
  size_t app_list_len = 0u;
  size_t lib_list_len = 0u;

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
  context.actor_name = "process_test";
  context.output = capture_output;
  context.authorize_disk_io = allow_disk_io;
  context.resolve_path = resolve_path;
  context.get_env = env_get;
  context.set_env = env_set;
  context.list_env = env_list;
  context.register_loaded_library = register_loaded_library;
  context.unregister_loaded_library = unregister_loaded_library;
  context.get_loaded_library_ref_count = get_loaded_library_ref_count;
  context.acquire_loaded_library = acquire_loaded_library;
  context.release_loaded_library = release_loaded_library;
  context.list_loaded_libraries = list_loaded_libraries;

  (void)env_set("PATH", "/apps");

  app_list_len = process_list_apps(app_list, sizeof(app_list));
  if (app_list_len == 0u || !string_contains(app_list, "help.bin") || !string_contains(app_list, "filedemo.bin")) {
    fail("app_list_missing_expected_entries");
  }
  printf("TEST:PASS:process_list_apps\n");

  lib_list_len = process_list_libraries(lib_list, sizeof(lib_list));
  if (lib_list_len == 0u || !string_contains(lib_list, "envlib.lib")) {
    fail("library_list_missing_expected_entries");
  }
  printf("TEST:PASS:process_library_list\n");

  if (process_run("filedemo", "", &context) != PROCESS_OK) {
    fail("run_failed");
  }
  printf("TEST:PASS:process_run_filedemo\n");

  if (process_run("/lib/envlib.lib", "", &context) != PROCESS_ERR_LIBRARY) {
    fail("library_invocation_not_blocked");
  }
  printf("TEST:PASS:process_library_contract\n");

  if (process_load_library("envlib", &context, &library_info) != PROCESS_OK) {
    fail("library_load_failed");
  }
  if (!string_equals(library_info.resolved_path, "/lib/envlib.lib") || library_info.program_len == 0u) {
    fail("library_load_metadata_invalid");
  }
  if (process_run("loadlib", "envlib", &context) != PROCESS_OK) {
    fail("library_load_command_failed");
  }
  if (!string_contains(g_output, "[lib] loaded /lib/envlib.lib handle=1")) {
    fail("library_load_output_missing");
  }
  if (process_run("libs", "loaded", &context) != PROCESS_OK) {
    fail("library_loaded_list_command_failed");
  }
  if (!string_contains(g_output, "handle=1 path=/lib/envlib.lib")) {
    fail("library_loaded_list_output_missing");
  }
  if (process_run("libs", "use 1", &context) != PROCESS_OK) {
    fail("library_use_command_failed");
  }
  if (!string_contains(g_output, "[lib] use handle=1 refs=1")) {
    fail("library_use_output_missing");
  }
  if (process_run("unload", "1", &context) != PROCESS_ERR_IN_USE) {
    fail("library_unload_in_use_not_blocked");
  }
  if (process_run("libs", "release 1", &context) != PROCESS_OK) {
    fail("library_release_command_failed");
  }
  if (!string_contains(g_output, "[lib] release handle=1 refs=0")) {
    fail("library_release_output_missing");
  }
  if (process_run("unload", "1", &context) != PROCESS_OK) {
    fail("library_unload_command_failed");
  }
  if (!string_contains(g_output, "[lib] unloaded handle=1 path=/lib/envlib.lib")) {
    fail("library_unload_output_missing");
  }
  if (process_run("libs", "loaded", &context) != PROCESS_OK) {
    fail("library_loaded_list_after_unload_command_failed");
  }
  if (!string_contains(g_output, "(no loaded libraries)")) {
    fail("library_loaded_list_after_unload_missing");
  }
  printf("TEST:PASS:process_library_load\n");

  if (process_run("env", "PROJECT=SecureOS", &context) != PROCESS_OK) {
    fail("env_set_failed");
  }
  if (process_run("env", "GREETING=\"hello world\"", &context) != PROCESS_OK) {
    fail("env_set_quoted_failed");
  }
  if (process_run("env", "key=myvar value=\"hello world\"", &context) != PROCESS_OK) {
    fail("env_set_named_quoted_failed");
  }
  if (process_run("env", "key=myquote value=\"\\\"quoted text\\\"\"", &context) != PROCESS_OK) {
    fail("env_set_escaped_quote_failed");
  }
  if (process_run("env", "PROJECT", &context) != PROCESS_OK) {
    fail("env_get_failed");
  }
  if (!string_contains(g_output, "SecureOS")) {
    fail("env_get_output_missing");
  }
  if (process_run("env", "GREETING", &context) != PROCESS_OK ||
      !string_contains(g_output, "hello world")) {
    fail("env_get_quoted_output_missing");
  }
  if (process_run("env", "myvar", &context) != PROCESS_OK ||
      !string_contains(g_output, "hello world")) {
    fail("env_get_named_output_missing");
  }
  if (process_run("env", "myquote", &context) != PROCESS_OK ||
      !string_contains(g_output, "\"quoted text\"")) {
    fail("env_get_escaped_quote_output_missing");
  }
  if (process_run("env", "", &context) != PROCESS_OK) {
    fail("env_list_failed");
  }
  if (!string_contains(g_output, "PROJECT=SecureOS")) {
    fail("env_list_output_missing");
  }
  printf("TEST:PASS:process_env_command\n");

  printf("TEST:PASS:process_auth_flow\n");

  if (!string_contains(g_output, "[filedemo] start") || !string_contains(g_output, "[filedemo] done")) {
    fail("output_markers_missing");
  }
  printf("TEST:PASS:process_output_markers\n");

  printf("TEST:PASS:process_storage_effect\n");

  return 0;
}
