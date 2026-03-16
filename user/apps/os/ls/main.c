/**
 * @file main.c
 * @brief "ls" shell command – lists directory contents.
 *
 * Purpose:
 *   Lists the files and subdirectories in the specified (or root)
 *   directory and prints the result to the console.
 *
 * Interactions:
 *   - secureos_api.h: calls os_fs_list_root and os_console_write
 *     through user-space system-call stubs.
 *   - app_runtime.c: loaded and executed by the kernel app runtime.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "ls [dir]"
 *   at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"

enum { BUF_MAX = 256, ARG_MAX = 128 };

int main(void) {
  char out[BUF_MAX];
  char args[ARG_MAX];
  const char *path = "/";

  args[0] = '\0';
  (void)os_get_args(args, (unsigned int)sizeof(args));
  if (args[0] != '\0') {
    path = args;
  }

  if (os_fs_list_root(out, (unsigned int)sizeof(out)) == OS_STATUS_OK) {
    (void)path;
    (void)os_console_write(out);
  }
  return 0;
}
