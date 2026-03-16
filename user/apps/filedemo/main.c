/**
 * @file main.c
 * @brief File-system demonstration application.
 *
 * Purpose:
 *   Exercises the SecureOS filesystem API by listing the root directory,
 *   reading a file, and performing write and append operations.  Serves
 *   as an integration smoke-test for the user-space file I/O path.
 *
 * Interactions:
 *   - secureos_api.h: calls os_fs_list_root, os_fs_read_file, and
 *     os_fs_write_file through the user-space system-call stubs.
 *   - app_runtime.c: loaded and executed by the kernel app runtime
 *     when the user runs the "filedemo" command.
 *
 * Launched by:
 *   Invoked as a user-space application via the console "run filedemo"
 *   command.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"

static void app_log(const char *message) {
  (void)os_console_write(message);
}

int main(void) {
  char list_output[256];
  char file_output[256];

  app_log("[filedemo] start\n");

  if (os_fs_list_root(list_output, sizeof(list_output)) == OS_STATUS_OK) {
    app_log("[filedemo] ls ok\n");
  }

  if (os_fs_read_file("readme.txt", file_output, sizeof(file_output)) == OS_STATUS_OK) {
    app_log("[filedemo] cat ok\n");
  }

  (void)os_fs_write_file("demo.txt", "hello", 0);
  (void)os_fs_write_file("demo.txt", " world", 1);

  app_log("[filedemo] done\n");
  return 0;
}
