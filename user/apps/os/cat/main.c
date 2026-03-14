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
