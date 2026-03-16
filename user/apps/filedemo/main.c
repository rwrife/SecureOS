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
 *   - secureos_api.h: calls os_fs_list_root and os_get_args through
 *     user-space system-call stubs.
 *   - lib/fslib.h: all file reads and writes go through fslib, which
 *     resolves paths against PWD via envlib for cwd-correct access.
 *     when the user runs the "filedemo" command.
 *
 * Launched by:
 *   Invoked as a user-space application via the console "run filedemo"
 *   command.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"
#include "lib/fslib.h"

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

  /* readme.txt lives at the filesystem root — always use its absolute path */
  if (fslib_read(FSLIB_HANDLE_INVALID, "/readme.txt", file_output, sizeof(file_output)) == FSLIB_STATUS_OK) {
    app_log("[filedemo] cat ok\n");
  }

  /* Write demo output relative to the current working directory via PWD */
  (void)fslib_write(FSLIB_HANDLE_INVALID, "demo.txt", "hello", 0);
  (void)fslib_write(FSLIB_HANDLE_INVALID, "demo.txt", " world", 1);

  app_log("[filedemo] done\n");
  return 0;
}
