/**
 * @file main.c
 * @brief "mkdir" shell command – creates a directory.
 *
 * Purpose:
 *   Creates a new directory at the specified path in the filesystem.
 *
 * Interactions:
 *   - secureos_api.h: calls os_get_args and os_console_write through
 *     user-space system-call stubs.
 *   - lib/fslib.h: directory creation goes through fslib for consistent
 *     path resolution against the current working directory.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types
 *   "mkdir <dir>" at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"
#include "lib/fslib.h"

enum { ARG_MAX = 128 };

int main(void) {
  char path[ARG_MAX];

  path[0] = '\0';
  (void)os_get_args(path, (unsigned int)sizeof(path));
  if (path[0] == '\0') {
    (void)os_console_write("usage: mkdir <dir>\n");
    return 1;
  }

  if (fslib_mkdir(FSLIB_HANDLE_INVALID, path) != FSLIB_STATUS_OK) {
    (void)os_console_write("mkdir failed\n");
    return 1;
  }

  return 0;
}
