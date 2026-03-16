/**
 * @file main.c
 * @brief "help" shell command – displays available commands.
 *
 * Purpose:
 *   Prints a summary of all available console commands and their usage
 *   to help the user navigate the SecureOS shell.
 *
 * Interactions:
 *   - secureos_api.h: calls os_console_write through user-space
 *     system-call stubs.
 *   - process.c: loaded and executed by the kernel process subsystem.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "help"
 *   at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"

int main(void) {
  (void)os_console_write("commands: help, ping, echo <text>, ls [dir], env <key> <value>, cat <file>, write <file> <text>, append <file> <text>, mkdir <dir>, cd <dir>, storage, apps, run <app>, exit <pass|fail>\n");
  return 0;
}
