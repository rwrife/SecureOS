/**
 * @file main.c
 * @brief "cd" shell command – changes the current working directory.
 *
 * Purpose:
 *   Changes the console's working directory to the specified path.
 *
 * Interactions:
 *   - secureos_api.h: calls os_change_directory and os_console_write
 *     through user-space system-call stubs.
 *   - app_runtime.c: loaded and executed by the kernel app runtime.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "cd <dir>"
 *   at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"

enum { ARG_MAX = 128 };

int main(void) {
  char path[ARG_MAX];

  path[0] = '\0';
  (void)os_get_args(path, (unsigned int)sizeof(path));
  if (path[0] == '\0') {
    (void)os_console_write("usage: cd <dir>\n");
    return 1;
  }

  if (os_process_chdir(path) != OS_STATUS_OK) {
    (void)os_console_write("cd failed\n");
    return 1;
  }

  return 0;
}
