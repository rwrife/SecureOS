/**
 * @file main.c
 * @brief "cd" shell command – changes the current working directory.
 *
 * Purpose:
 *   Changes the console's working directory to the specified path.
 *
 * Interactions:
 *   - lib/fslib.h: calls fslib_chdir and status helpers.
 *   - secureos_api.h: used indirectly through fslib wrappers.
 *   - process.c: loaded and executed by the kernel process subsystem.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "cd <dir>"
 *   at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"
#include "lib/fslib.h"

enum { ARG_MAX = 128 };

int main(void) {
  char path[ARG_MAX];

  path[0] = '\0';
  (void)os_get_args(path, (unsigned int)sizeof(path));
  if (path[0] == '\0') {
    (void)os_console_write("usage: cd <dir>\n");
    return 1;
  }

  if (fslib_chdir(FSLIB_HANDLE_INVALID, path) != FSLIB_STATUS_OK) {
    (void)os_console_write("cd failed\n");
    return 1;
  }

  return 0;
}
