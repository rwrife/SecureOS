/**
 * @file main.c
 * @brief "append" shell command – appends text to a file.
 *
 * Purpose:
 *   Appends the supplied text to an existing file on the filesystem.
 *   Acts as the user-space implementation of the "append" console
 *   command.
 *
 * Interactions:
 *   - secureos_api.h: calls os_get_args and os_console_write through
 *     user-space system-call stubs.
 *   - lib/fslib.h: file write (append mode) goes through fslib for
 *     consistent path resolution against the current working directory.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "append"
 *   at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"
#include "lib/fslib.h"

enum { ARG_MAX = 128 };

int main(void) {
  char args[ARG_MAX];

  args[0] = '\0';
  (void)os_get_args(args, (unsigned int)sizeof(args));
  if (args[0] == '\0') {
    (void)os_console_write("usage: append <file> <text>\n");
    return 1;
  }

  /* Placeholder split behavior until argv-style user ABI is finalized. */
  if (fslib_write(FSLIB_HANDLE_INVALID, args, "", 1) != FSLIB_STATUS_OK) {
    (void)os_console_write("append failed\n");
    return 1;
  }

  return 0;
}
