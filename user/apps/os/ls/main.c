/**
 * @file main.c
 * @brief "ls" shell command – lists directory contents.
 *
 * Purpose:
 *   Lists the files and subdirectories in the specified (or root)
 *   directory and prints the result to the console.
 *
 * Interactions:
 *   - lib/fslib.h: calls fslib_list/fslib_list_cwd for directory views.
 *   - secureos_api.h: used indirectly via fslib plus os_console_write.
 *   - app_runtime.c: loaded and executed by the kernel app runtime.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "ls [dir]"
 *   at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"
#include "lib/fslib.h"

enum { BUF_MAX = 256, ARG_MAX = 128 };

int main(void) {
  char out[BUF_MAX];
  char args[ARG_MAX];
  fslib_status_t status = FSLIB_STATUS_ERROR;

  args[0] = '\0';
  (void)os_get_args(args, (unsigned int)sizeof(args));

  if (args[0] == '\0') {
    status = fslib_list_cwd(FSLIB_HANDLE_INVALID, out, (unsigned int)sizeof(out));
  } else {
    status = fslib_list(FSLIB_HANDLE_INVALID, args, out, (unsigned int)sizeof(out));
  }

  if (status != FSLIB_STATUS_OK) {
    (void)os_console_write("ls failed\n");
    return 1;
  }

  (void)os_console_write(out);
  return 0;
}
