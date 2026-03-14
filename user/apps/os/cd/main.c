#include "secureos_api.h"

enum { ARG_MAX = 128 };

int main(void) {
  char path[ARG_MAX];

  path[0] = '\0';
  (void)os_get_args(path, (unsigned int)sizeof(path));
  if (path[0] == '\0') {
    (void)os_console_write("usage: cd <dir>\n");
    return 1;
  }

  if (os_process_chdir(path) != OS_STATUS_OK) {
    (void)os_console_write("cd failed\n");
    return 1;
  }

  return 0;
}
