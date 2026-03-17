/**
 * @file main.c
 * @brief "ping" shell command – simple connectivity test.
 *
 * Purpose:
 *   Uses the shared netlib contract to issue a host reachability check
 *   through the SecureOS networking stack.
 *
 * Interactions:
 *   - lib/netlib.h: calls shared networking helpers through the netlib
 *     contract.
 *   - secureos_api.h: uses console and argument syscall stubs.
 *   - process.c: loaded and executed by the kernel process subsystem.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "ping"
 *   at the console.  Built as a standalone ELF binary.
 */

#include "lib/netlib.h"

enum {
  ARG_MAX = 128,
};

static void print_text(const char *value) {
  (void)os_console_write(value);
}

int main(void) {
  char args[ARG_MAX];
  char output[256];
  netlib_status_t status;

  args[0] = '\0';
  output[0] = '\0';
  (void)os_get_args(args, (unsigned int)sizeof(args));
  if (args[0] == '\0') {
    print_text("usage: ping <host>\n");
    return 1;
  }

  status = netlib_ping(NETLIB_HANDLE_INVALID,
                       args,
                       output,
                       (unsigned int)sizeof(output));
  if (status != NETLIB_STATUS_OK) {
    print_text("ping failed\n");
    return 1;
  }

  if (output[0] != '\0') {
    print_text(output);
    if (output[0] != '\0') {
      unsigned int last = 0u;
      while (output[last] != '\0') {
        ++last;
      }
      if (last == 0u || output[last - 1u] != '\n') {
        print_text("\n");
      }
    }
  }
  return 0;
}
