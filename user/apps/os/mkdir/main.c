/**
 * @file main.c
 * @brief "mkdir" shell command – creates a directory.
 *
 * Purpose:
 *   Creates a new directory at the specified path in the filesystem.
 *
 * Interactions:
 *   - secureos_api.h: calls os_fs_mkdir and os_console_write through
 *     user-space system-call stubs.
 *   - app_runtime.c: loaded and executed by the kernel app runtime.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types
 *   "mkdir <dir>" at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"

enum { ARG_MAX = 128 };

int main(void) {
  char path[ARG_MAX];

  path[0] = '\0';
  (void)os_get_args(path, (unsigned int)sizeof(path));
  if (path[0] == '\0') {
    (void)os_console_write("usage: mkdir <dir>\n");
    return 1;
  }

  if (os_fs_mkdir(path) != OS_STATUS_OK) {
    (void)os_console_write("mkdir failed\n");
    return 1;
  }

  return 0;
}
