/**
 * @file user/apps/cc/main.c
 * @brief In-OS `cc` driver scaffold entrypoint.
 *
 * Purpose:
 *   Provides the first executable skeleton for the in-OS compiler command.
 *   This slice deliberately ships stable `--help` and `--version` surfaces,
 *   and returns a deterministic non-zero diagnostic for compile invocations
 *   until TinyCC freestanding wiring (#408) lands.
 *
 * Interactions:
 *   - secureos_api.h: uses os_get_args/os_console_write user-runtime stubs.
 *   - process.c: loaded by the kernel process subsystem as a standalone app.
 *
 * Launched by:
 *   Invoked as a user-space app when the user runs `cc`.
 */

#include "secureos_api.h"

enum {
  CC_ARGS_MAX = 256,
};

static int cc_is_space(char value) {
  return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

static const char *cc_skip_spaces(const char *value) {
  while (value != 0 && *value != '\0' && cc_is_space(*value)) {
    ++value;
  }
  return value;
}

static void cc_copy_first_token(const char *src, char *out, unsigned int out_size) {
  unsigned int i = 0u;

  if (out == 0 || out_size == 0u) {
    return;
  }

  if (src == 0) {
    out[0] = '\0';
    return;
  }

  src = cc_skip_spaces(src);
  while (src[i] != '\0' && !cc_is_space(src[i]) && i + 1u < out_size) {
    out[i] = src[i];
    ++i;
  }
  out[i] = '\0';
}

static int cc_string_equals(const char *left, const char *right) {
  unsigned int i = 0u;
  if (left == 0 || right == 0) {
    return 0;
  }
  while (left[i] != '\0' && right[i] != '\0') {
    if (left[i] != right[i]) {
      return 0;
    }
    ++i;
  }
  return left[i] == right[i];
}

static void cc_print_help(void) {
  (void)os_console_write("usage: cc <input.c> -o <output.bin> [--manifest <path>]\n");
  (void)os_console_write("       cc --help\n");
  (void)os_console_write("       cc --version\n");
  (void)os_console_write("\n");
  (void)os_console_write("status: compile path not yet wired (awaiting #408 Phase 3)\n");
}

int main(void) {
  char args[CC_ARGS_MAX];
  char token[32];

  args[0] = '\0';
  token[0] = '\0';

  (void)os_get_args(args, (unsigned int)sizeof(args));
  cc_copy_first_token(args, token, (unsigned int)sizeof(token));

  if (token[0] == '\0' || cc_string_equals(token, "--help") || cc_string_equals(token, "-h")) {
    cc_print_help();
    return token[0] == '\0' ? 1 : 0;
  }

  if (cc_string_equals(token, "--version")) {
    (void)os_console_write("cc 0.1.0-stub\n");
    return 0;
  }

  (void)os_console_write("cc: in-OS compile not yet wired (awaiting #408 Phase 3)\n");
  return 2;
}
