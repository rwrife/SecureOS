/**
 * @file main.c
 * @brief "apps" shell command – lists installed applications.
 *
 * Purpose:
 *   Queries the kernel for the list of available user-space applications
 *   and prints the result to the console.
 *
 * Interactions:
 *   - secureos_api.h: calls os_apps_list and os_console_write through
 *     user-space system-call stubs.
 *   - app_runtime.c: loaded and executed by the kernel app runtime.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "apps"
 *   at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"

enum { BUF_MAX = 256 };

int main(void) {
  char out[BUF_MAX];

  if (os_apps_list(out, (unsigned int)sizeof(out)) == OS_STATUS_OK) {
    (void)os_console_write(out);
  }
  return 0;
}
