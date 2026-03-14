#include "secureos_api.h"

enum { ARG_MAX = 128 };

int main(void) {
  char path[ARG_MAX];

  path[0] = '\0';
  (void)os_get_args(path, (unsigned int)sizeof(path));
  if (path[0] == '\0') {
    (void)os_console_write("usage: mkdir <dir>\n");
    return 1;
  }

  if (os_fs_mkdir(path) != OS_STATUS_OK) {
    (void)os_console_write("mkdir failed\n");
    return 1;
  }

  return 0;
}
