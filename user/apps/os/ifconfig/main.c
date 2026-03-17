/**
 * @file main.c
 * @brief "ifconfig" shell command - displays network interface details.
 *
 * Purpose:
 *   Uses the shared netlib contract to fetch and print network interface
 *   state and addressing information.
 *
 * Interactions:
 *   - lib/netlib.h: calls the shared interface-info helper through netlib.
 *   - secureos_api.h: uses console output syscall stubs.
 *   - process.c: loaded and executed by the kernel process subsystem.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "ifconfig"
 *   at the console. Built as a standalone ELF binary.
 */

#include "lib/netlib.h"

enum {
  OUT_MAX = 256,
};

static void print_text(const char *value) {
  (void)os_console_write(value);
}

static int has_trailing_newline(const char *value) {
  unsigned int i = 0u;

  if (value == 0 || value[0] == '\0') {
    return 1;
  }

  while (value[i] != '\0') {
    ++i;
  }

  return i > 0u && value[i - 1u] == '\n';
}

int main(void) {
  char output[OUT_MAX];
  netlib_status_t status;

  output[0] = '\0';
  status = netlib_ifconfig(NETLIB_HANDLE_INVALID,
                           output,
                           (unsigned int)sizeof(output));
  if (status != NETLIB_STATUS_OK) {
    print_text("ifconfig failed\n");
    return 1;
  }

  if (output[0] != '\0') {
    print_text(output);
    if (!has_trailing_newline(output)) {
      print_text("\n");
    }
  }

  return 0;
}