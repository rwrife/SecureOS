/**
 * @file main.c
 * @brief "ping" shell command – simple connectivity test.
 *
 * Purpose:
 *   Replies with "pong" to confirm the console and app-runtime
 *   pipeline is functioning.
 *
 * Interactions:
 *   - secureos_api.h: calls os_console_write through user-space
 *     system-call stubs.
 *   - process.c: loaded and executed by the kernel process subsystem.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "ping"
 *   at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"

int main(void) {
  (void)os_console_write("pong\n");
  return 0;
}
