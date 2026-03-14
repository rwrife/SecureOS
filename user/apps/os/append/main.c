#include "secureos_api.h"

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
  if (os_fs_write_file(args, "", 1) != OS_STATUS_OK) {
    (void)os_console_write("append failed\n");
    return 1;
  }

  return 0;
}
