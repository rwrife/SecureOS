/**
 * @file main.c
 * @brief "help" shell command – displays available commands.
 *
 * Purpose:
 *   Prints command help from build-time embedded text resources so
 *   command documentation can be edited as text files and compiled
 *   into the help application.
 *
 * Interactions:
 *   - secureos_api.h: calls os_get_args and os_console_write through
 *     user-space system-call stubs.
 *   - build_user_app.{sh,ps1}: generates and compiles a resource source
 *     file from text files in user/apps/os/help/resources/.
 *   - process.c: loaded and executed by the kernel process subsystem.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types "help"
 *   at the console.  Built as a standalone ELF binary.
 */

#include "secureos_api.h"

enum {
  ARG_MAX = 128,
  KEY_MAX = 32,
};

typedef struct {
  const char *name;
  const char *text;
} help_resource_entry_t;

extern const help_resource_entry_t g_help_resources[];
extern const unsigned int g_help_resource_count;

static int char_is_space(char value) {
  return value == ' ' || value == '\t';
}

static const char *skip_spaces(const char *value) {
  while (value != 0 && *value != '\0' && char_is_space(*value)) {
    ++value;
  }
  return value;
}

static int string_equals(const char *left, const char *right) {
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

static void copy_first_token(const char *src, char *out, unsigned int out_size) {
  unsigned int i = 0u;
  if (out == 0 || out_size == 0u) {
    return;
  }

  if (src == 0) {
    out[0] = '\0';
    return;
  }

  src = skip_spaces(src);
  while (src[i] != '\0' && !char_is_space(src[i]) && i + 1u < out_size) {
    out[i] = src[i];
    ++i;
  }
  out[i] = '\0';
}

static int text_has_trailing_newline(const char *value) {
  unsigned int i = 0u;
  if (value == 0 || value[0] == '\0') {
    return 1;
  }
  while (value[i] != '\0') {
    ++i;
  }
  return i > 0u && value[i - 1u] == '\n';
}

static int print_help_for(const char *key) {
  unsigned int i = 0u;
  for (i = 0u; i < g_help_resource_count; ++i) {
    if (string_equals(g_help_resources[i].name, key)) {
      (void)os_console_write(g_help_resources[i].text);
      if (!text_has_trailing_newline(g_help_resources[i].text)) {
        (void)os_console_write("\n");
      }
      return 1;
    }
  }
  return 0;
}

int main(void) {
  char args[ARG_MAX];
  char key[KEY_MAX];

  args[0] = '\0';
  key[0] = '\0';
  (void)os_get_args(args, (unsigned int)sizeof(args));
  copy_first_token(args, key, (unsigned int)sizeof(key));

  if (key[0] == '\0') {
    key[0] = 'i';
    key[1] = 'n';
    key[2] = 'd';
    key[3] = 'e';
    key[4] = 'x';
    key[5] = '\0';
  }

  if (print_help_for(key)) {
    return 0;
  }

  (void)os_console_write("no help entry for command: ");
  (void)os_console_write(key);
  (void)os_console_write("\n");
  (void)os_console_write("try: help index\n");
  return 1;
}
