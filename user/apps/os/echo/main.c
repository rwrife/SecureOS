#include "secureos_api.h"

enum { ARG_MAX = 128 };

int main(void) {
  char args[ARG_MAX];

  args[0] = '\0';
  (void)os_get_args(args, (unsigned int)sizeof(args));
  (void)os_console_write(args);
  (void)os_console_write("\n");
  return 0;
}
