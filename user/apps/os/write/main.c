/**
 * @file main.c
 * @brief "write" shell command – writes text to a file.
 *
 * Purpose:
 *   Creates or overwrites a file with the supplied text content.
 *
 * Interactions:
 *   - secureos_api.h: calls os_get_args and os_console_write through
 *     user-space system-call stubs.
 *   - lib/fslib.h: file write goes through fslib for consistent path
 *     resolution against the current working directory.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types
 *   "write <file> <text>" at the console.  Built as a standalone
 *   ELF binary.
 */

#include "secureos_api.h"
#include "lib/fslib.h"

enum { ARG_MAX = 128 };

int main(void) {
  char args[ARG_MAX];

  args[0] = '\0';
  (void)os_get_args(args, (unsigned int)sizeof(args));
  if (args[0] == '\0') {
    (void)os_console_write("usage: write <file> <text>\n");
    return 1;
  }

  /* Placeholder split behavior until argv-style user ABI is finalized. */
  if (fslib_write(FSLIB_HANDLE_INVALID, args, "", 0) != FSLIB_STATUS_OK) {
    (void)os_console_write("write failed\n");
    return 1;
  }

  return 0;
}
