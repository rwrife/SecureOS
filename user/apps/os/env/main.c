/**
 * @file main.c
 * @brief "env" shell command – manages environment variables.
 *
 * Purpose:
 *   Gets, sets, or lists per-session environment variables.  Supports
 *   key=value syntax, quoted values, and named-argument form
 *   (key=<name> value=<val>).  Without arguments, lists all variables.
 *
 * Interactions:
 *   - secureos_api.h: calls os_get_args and os_console_write through
 *     user-space system-call stubs.
 *   - lib/envlib.h: all environment variable reads, writes, and listings
 *     go through envlib for a single consistent maintenance point.
 *
 * Launched by:
 *   Invoked as a user-space application when the user types
 *   "env [key[=value]]" at the console.  Built as a standalone
 *   ELF binary.
 */

#include "secureos_api.h"
#include "lib/envlib.h"

enum {
  ARG_MAX = 128,
  OUT_MAX = 256,
};

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

static int read_quoted(const char *src, char *out, unsigned int out_size, const char **out_next) {
  unsigned int i = 1u;
  unsigned int cursor = 0u;

  if (src == 0 || src[0] != '"' || out == 0 || out_size == 0u) {
    return 0;
  }

  out[0] = '\0';
  while (src[i] != '\0') {
    char ch = src[i];
    if (ch == '"') {
      if (out_next != 0) {
        *out_next = src + i + 1u;
      }
      out[cursor] = '\0';
      return 1;
    }

    if ((ch == '\\' || ch == '/') && (src[i + 1u] == '"' || src[i + 1u] == '\\' || src[i + 1u] == '/')) {
      ch = src[i + 1u];
      i += 2u;
    } else {
      ++i;
    }

    if (cursor + 1u < out_size) {
      out[cursor++] = ch;
      out[cursor] = '\0';
    }
  }

  return 0;
}

static void copy_unquoted_token(const char *src, char *out, unsigned int out_size, const char **out_next) {
  unsigned int i = 0u;

  if (out == 0 || out_size == 0u) {
    if (out_next != 0) {
      *out_next = src;
    }
    return;
  }

  while (src[i] != '\0' && !char_is_space(src[i]) && i + 1u < out_size) {
    out[i] = src[i];
    ++i;
  }
  out[i] = '\0';

  if (out_next != 0) {
    *out_next = src + i;
  }
}

static int extract_named_arg(const char *args, const char *name, char *out, unsigned int out_size) {
  const char *cursor = skip_spaces(args);

  if (args == 0 || name == 0 || out == 0 || out_size == 0u) {
    return 0;
  }

  out[0] = '\0';
  while (cursor[0] != '\0') {
    char token[ARG_MAX];
    unsigned int token_len = 0u;
    const char *value_start = 0;
    const char *next = 0;

    while (cursor[token_len] != '\0' && cursor[token_len] != '=' && !char_is_space(cursor[token_len]) &&
           token_len + 1u < (unsigned int)sizeof(token)) {
      token[token_len] = cursor[token_len];
      ++token_len;
    }
    token[token_len] = '\0';

    if (cursor[token_len] == '=') {
      value_start = cursor + token_len + 1u;
      if (value_start[0] == '"') {
        if (!read_quoted(value_start, out, out_size, &next)) {
          return 0;
        }
      } else {
        copy_unquoted_token(value_start, out, out_size, &next);
      }

      if (string_equals(token, name)) {
        return 1;
      }

      cursor = skip_spaces(next);
      continue;
    }

    while (cursor[token_len] != '\0' && !char_is_space(cursor[token_len])) {
      ++token_len;
    }
    cursor = skip_spaces(cursor + token_len);
  }

  return 0;
}

static int find_eq_before_space(const char *value, unsigned int *out_pos) {
  unsigned int i = 0u;

  if (value == 0 || out_pos == 0) {
    return 0;
  }

  while (value[i] != '\0') {
    if (value[i] == '=') {
      *out_pos = i;
      return 1;
    }
    if (char_is_space(value[i])) {
      return 0;
    }
    ++i;
  }

  return 0;
}

int main(void) {
  char args[ARG_MAX];
  char out[OUT_MAX];
  char key[ARG_MAX];
  char named_key[ARG_MAX];
  char named_value[OUT_MAX];
  char value[OUT_MAX];
  const char *trimmed = 0;
  const char *next = 0;
  unsigned int i = 0u;
  unsigned int eq_pos = 0u;
  unsigned int j = 0u;

  args[0] = '\0';
  (void)os_get_args(args, (unsigned int)sizeof(args));
  trimmed = skip_spaces(args);

  if (trimmed[0] == '\0') {
    if (envlib_list(ENVLIB_HANDLE_INVALID, out, (unsigned int)sizeof(out)) == ENVLIB_STATUS_OK) {
      (void)os_console_write(out);
    }
    return 0;
  }

  if (extract_named_arg(trimmed, "key", named_key, (unsigned int)sizeof(named_key))) {
    if (extract_named_arg(trimmed, "value", named_value, (unsigned int)sizeof(named_value))) {
      return envlib_set(ENVLIB_HANDLE_INVALID, named_key, named_value) == ENVLIB_STATUS_OK ? 0 : 1;
    }

    if (envlib_get(ENVLIB_HANDLE_INVALID, named_key, out, (unsigned int)sizeof(out)) != ENVLIB_STATUS_OK) {
      return 1;
    }
    while (out[j] != '\0' && j + 2u < (unsigned int)sizeof(out)) {
      ++j;
    }
    out[j++] = '\n';
    out[j] = '\0';
    (void)os_console_write(out);
    return 0;
  }

  if (find_eq_before_space(trimmed, &eq_pos)) {
    if (eq_pos == 0u || eq_pos + 1u >= (unsigned int)sizeof(key)) {
      return 1;
    }

    for (i = 0u; i < eq_pos && i + 1u < (unsigned int)sizeof(key); ++i) {
      key[i] = trimmed[i];
    }
    key[i] = '\0';

    trimmed = trimmed + eq_pos + 1u;
    if (trimmed[0] == '"') {
      if (!read_quoted(trimmed, value, (unsigned int)sizeof(value), &next)) {
        return 1;
      }
      if (skip_spaces(next)[0] != '\0') {
        return 1;
      }
      return envlib_set(ENVLIB_HANDLE_INVALID, key, value) == ENVLIB_STATUS_OK ? 0 : 1;
    }

    return envlib_set(ENVLIB_HANDLE_INVALID, key, trimmed) == ENVLIB_STATUS_OK ? 0 : 1;
  }

  while (trimmed[i] != '\0' && !char_is_space(trimmed[i]) && i + 1u < (unsigned int)sizeof(key)) {
    key[i] = trimmed[i];
    ++i;
  }
  key[i] = '\0';

  trimmed = skip_spaces(trimmed + i);
  if (trimmed[0] != '\0') {
    if (trimmed[0] == '"') {
      if (!read_quoted(trimmed, value, (unsigned int)sizeof(value), &next)) {
        return 1;
      }
      if (skip_spaces(next)[0] != '\0') {
        return 1;
      }
      return envlib_set(ENVLIB_HANDLE_INVALID, key, value) == ENVLIB_STATUS_OK ? 0 : 1;
    }
    return envlib_set(ENVLIB_HANDLE_INVALID, key, trimmed) == ENVLIB_STATUS_OK ? 0 : 1;
  }

  if (envlib_get(ENVLIB_HANDLE_INVALID, key, out, (unsigned int)sizeof(out)) != ENVLIB_STATUS_OK) {
    return 1;
  }

  while (out[j] != '\0' && j + 2u < (unsigned int)sizeof(out)) {
    ++j;
  }
  out[j++] = '\n';
  out[j] = '\0';
  (void)os_console_write(out);
  return 0;
}
