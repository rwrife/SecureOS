/**
 * @file main.c
 * @brief "storage" shell command – displays storage backend information.
 *
 * Purpose:
 *   Queries the kernel for the active storage backend's status
 *   (type, block count, block size) and prints it to the console.
 *
 * Interactions:
 *   - secureos_api.h: calls os_storage_info and os_console_write
 *     through user-space system-call stubs.
 *   - app_runtime.c: loaded and executed by the kernel app runtime.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "storage"
 *   at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"

enum { BUF_MAX = 256 };

int main(void) {
  char out[BUF_MAX];

  if (os_storage_info(out, (unsigned int)sizeof(out)) == OS_STATUS_OK) {
    (void)os_console_write(out);
  }
  return 0;
}
