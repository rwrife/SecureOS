/**
 * @file main.c
 * @brief "echo" shell command – prints arguments to the console.
 *
 * Purpose:
 *   Writes its command-line arguments back to the console output,
 *   followed by a newline.
 *
 * Interactions:
 *   - secureos_api.h: calls os_get_args and os_console_write through
 *     user-space system-call stubs.
 *   - process.c: loaded and executed by the kernel process subsystem.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types
 *   "echo <text>" at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"

enum { ARG_MAX = 128 };

int main(void) {
  char args[ARG_MAX];

  args[0] = '\0';
  (void)os_get_args(args, (unsigned int)sizeof(args));
  (void)os_console_write(args);
  (void)os_console_write("\n");
  return 0;
}
