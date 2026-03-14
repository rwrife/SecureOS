#include "secureos_api.h"

int main(void) {
  (void)os_console_write("commands: help, ping, echo <text>, ls [dir], cat <file>, write <file> <text>, append <file> <text>, mkdir <dir>, cd <dir>, storage, apps, run <app>, exit <pass|fail>\n");
  return 0;
}
