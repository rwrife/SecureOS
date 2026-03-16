/**
 * @file main.c
 * @brief "cat" shell command – displays file contents.
 *
 * Purpose:
 *   Reads the contents of a named file from the filesystem and prints
 *   it to the console.
 *
 * Interactions:
 *   - secureos_api.h: calls os_fs_read_file and os_console_write
 *     through user-space system-call stubs.
 *   - app_runtime.c: loaded and executed by the kernel app runtime.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "cat <file>"
 *   at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"

enum { BUF_MAX = 256, ARG_MAX = 128 };

int main(void) {
  char out[BUF_MAX];
  char path[ARG_MAX];

  path[0] = '\0';
  (void)os_get_args(path, (unsigned int)sizeof(path));
  if (path[0] == '\0') {
    (void)os_console_write("usage: cat <file>\n");
    return 1;
  }

  if (os_fs_read_file(path, out, (unsigned int)sizeof(out)) != OS_STATUS_OK) {
    (void)os_console_write("not found\n");
    return 1;
  }

  (void)os_console_write(out);
  (void)os_console_write("\n");
  return 0;
}
