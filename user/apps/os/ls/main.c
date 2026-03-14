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
