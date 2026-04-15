/**
 * @file main.c
 * @brief "http" shell command - issues a blocking HTTP request.
 *
 * Purpose:
 *   Uses the shared netlib contract to issue an HTTP request and print the
 *   returned response buffer to the console.
 *
 * Interactions:
 *   - lib/netlib.h: calls the shared HTTP helper through the netlib contract.
 *   - secureos_api.h: uses console and argument syscall stubs.
 *   - process.c: loaded and executed by the kernel process subsystem.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "http"
 *   at the console. Built as a standalone ELF binary.
 */

#include "lib/netlib.h"

enum {
  ARG_MAX = 256,
  OUT_MAX = 512,
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
  char args[ARG_MAX];
  char output[OUT_MAX];
  netlib_status_t status;

  args[0] = '\0';
  output[0] = '\0';
  (void)os_get_args(args, (unsigned int)sizeof(args));
  if (args[0] == '\0') {
    print_text("usage: http <url>\n");
    print_text("  Supports http:// and https:// URLs.\n");
    return 1;
  }

  if (netlib_device_ready(NETLIB_HANDLE_INVALID) != NETLIB_STATUS_OK) {
    print_text("http: no network device available\n");
    return 1;
  }

  status = netlib_http_get(NETLIB_HANDLE_INVALID,
                           args,
                           output,
                           (unsigned int)sizeof(output));
  if (status != NETLIB_STATUS_OK) {
    print_text("http failed\n");
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