#include "secureos_api.h"

enum { BUF_MAX = 256 };

int main(void) {
  char out[BUF_MAX];

  if (os_storage_info(out, (unsigned int)sizeof(out)) == OS_STATUS_OK) {
    (void)os_console_write(out);
  }
  return 0;
}
