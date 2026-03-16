/**
 * @file app_runtime.c
 * @brief User-space application loader and script interpreter.
 *
 * Purpose:
 *   Loads ELF binaries from the filesystem, validates their format,
 *   and executes them.  Supports both native user-space entry points
 *   and a built-in script interpreter for lightweight shell commands.
 *   Manages per-invocation argument passing, environment variable
 *   expansion, and capability-gated disk I/O authorization.
 *
 * Interactions:
 *   - fs_service.c: reads application ELF files from the filesystem
 *     via fs_read_file_bytes.
 *   - cap_table.c: checks CAP_APP_EXEC before allowing execution and
 *     CAP_DISK_IO_REQUEST for storage operations.
 *   - console.c: the console dispatches user commands through
 *     app_runtime_run, providing output and authorization callbacks.
 *   - storage_hal.c: storage info queries are forwarded to the HAL.
 *
 * Launched by:
 *   app_runtime_run() is called by console.c when the user invokes a
 *   command.  Not a standalone process; compiled into the kernel image.
 */

#include "app_runtime.h"

#include <stdint.h>

#include "../cap/cap_table.h"
#include "../fs/fs_service.h"
#include "../hal/storage_hal.h"

enum {
  APP_FILE_MAX = 512,
  APP_OUTPUT_MAX = 512,
  APP_LINE_MAX = 192,
  APP_TOKEN_MAX = 64,
  APP_ARGS_MAX = 128,
  APP_ARGV_MAX = 8,
};

typedef struct {
  char args_buffer[APP_ARGS_MAX];
  const char *argv[APP_ARGV_MAX];
  size_t argc;
  const char *raw_args;
} app_invocation_args_t;

static app_runtime_result_t app_parse_elf_program(const uint8_t *elf_data,
                                                  size_t elf_len,
                                                  const uint8_t **out_program,
                                                  size_t *out_program_len);

static int app_string_equals(const char *left, const char *right) {
  while (*left != '\0' && *right != '\0') {
    if (*left != *right) {
      return 0;
    }
    ++left;
    ++right;
  }

  return *left == *right;
}

static size_t app_string_len(const char *value) {
  size_t len = 0u;

  if (value == 0) {
    return 0u;
  }

  while (value[len] != '\0') {
    ++len;
  }

  return len;
}

static int app_string_starts_with(const char *value, const char *prefix) {
  if (value == 0 || prefix == 0) {
    return 0;
  }

  while (*prefix != '\0') {
    if (*value == '\0' || *value != *prefix) {
      return 0;
    }
    ++value;
    ++prefix;
  }

  return 1;
}

static int app_string_ends_with(const char *value, const char *suffix) {
  size_t value_len = 0u;
  size_t suffix_len = 0u;

  if (value == 0 || suffix == 0) {
    return 0;
  }

  value_len = app_string_len(value);
  suffix_len = app_string_len(suffix);
  if (suffix_len > value_len) {
    return 0;
  }

  return app_string_equals(value + (value_len - suffix_len), suffix);
}

static int app_char_is_space(char value) {
  return value == ' ' || value == '\t';
}

static const char *app_skip_spaces(const char *value) {
  while (*value != '\0' && app_char_is_space(*value)) {
    ++value;
  }
  return value;
}

static void app_copy_string(char *dst, size_t dst_size, const char *src) {
  size_t i = 0u;

  if (dst == 0 || dst_size == 0u) {
    return;
  }

  while (src[i] != '\0' && i + 1u < dst_size) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

static void app_copy_until_space(char *dst, size_t dst_size, const char *src, const char **out_next) {
  size_t i = 0u;

  if (dst == 0 || dst_size == 0u) {
    if (out_next != 0) {
      *out_next = src;
    }
    return;
  }

  while (src[i] != '\0' && !app_char_is_space(src[i]) && i + 1u < dst_size) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';

  if (out_next != 0) {
    *out_next = src + i;
  }
}

static const char *app_find_char(const char *value, char needle) {
  if (value == 0) {
    return 0;
  }

  while (*value != '\0') {
    if (*value == needle) {
      return value;
    }
    ++value;
  }

  return 0;
}

static size_t app_append_string(char *dst, size_t dst_size, size_t cursor, const char *src) {
  size_t i = 0u;

  if (dst == 0 || src == 0) {
    return cursor;
  }

  while (src[i] != '\0' && cursor + 1u < dst_size) {
    dst[cursor++] = src[i++];
  }

  if (cursor < dst_size) {
    dst[cursor] = '\0';
  }
  return cursor;
}

static size_t app_append_u32_decimal(char *dst, size_t dst_size, size_t cursor, uint32_t value) {
  char digits[10];
  size_t count = 0u;
  size_t i = 0u;

  if (value == 0u) {
    if (cursor + 1u < dst_size) {
      dst[cursor++] = '0';
      dst[cursor] = '\0';
    }
    return cursor;
  }

  while (value > 0u && count < sizeof(digits)) {
    digits[count++] = (char)('0' + (value % 10u));
    value /= 10u;
  }

  for (i = 0u; i < count; ++i) {
    if (cursor + 1u >= dst_size) {
      break;
    }
    dst[cursor++] = digits[count - i - 1u];
  }

  if (cursor < dst_size) {
    dst[cursor] = '\0';
  }
  return cursor;
}

static unsigned int app_parse_u32(const char *value, int *ok) {
  unsigned int parsed = 0u;
  size_t i = 0u;

  if (ok != 0) {
    *ok = 0;
  }
  if (value == 0 || value[0] == '\0') {
    return 0u;
  }

  while (value[i] != '\0') {
    if (value[i] < '0' || value[i] > '9') {
      return 0u;
    }
    parsed = (parsed * 10u) + (unsigned int)(value[i] - '0');
    ++i;
  }

  if (ok != 0) {
    *ok = 1;
  }
  return parsed;
}

static uint16_t app_read_u16(const uint8_t *buffer, size_t offset) {
  return (uint16_t)((uint16_t)buffer[offset] | ((uint16_t)buffer[offset + 1u] << 8u));
}

static uint32_t app_read_u32(const uint8_t *buffer, size_t offset) {
  return (uint32_t)((uint32_t)buffer[offset] |
                    ((uint32_t)buffer[offset + 1u] << 8u) |
                    ((uint32_t)buffer[offset + 2u] << 16u) |
                    ((uint32_t)buffer[offset + 3u] << 24u));
}

static void app_emit(const app_runtime_context_t *context, const char *message) {
  if (context != 0 && context->output != 0) {
    context->output(message);
  }
}

static int app_require_capability(cap_subject_id_t subject_id, capability_id_t capability_id) {
  return cap_table_check(subject_id, capability_id) == CAP_OK;
}

static app_runtime_result_t app_require_storage_access(const app_runtime_context_t *context,
                                                       capability_id_t capability_id,
                                                       const char *operation,
                                                       const char *path) {
  if (!app_require_capability(context->subject_id, capability_id)) {
    return APP_RUNTIME_ERR_CAPABILITY;
  }

  if (!app_require_capability(context->subject_id, CAP_DISK_IO_REQUEST)) {
    return APP_RUNTIME_ERR_CAPABILITY;
  }

  if (context->authorize_disk_io == 0) {
    return APP_RUNTIME_ERR_DENIED;
  }

  if (context->authorize_disk_io(operation, path) != CAP_ACCESS_ALLOW) {
    return APP_RUNTIME_ERR_DENIED;
  }

  return APP_RUNTIME_OK;
}

static void app_resolve_path(const app_runtime_context_t *context,
                             const char *input_path,
                             char *out_path,
                             size_t out_path_size) {
  if (out_path == 0 || out_path_size == 0u) {
    return;
  }

  if (context != 0 && context->resolve_path != 0) {
    context->resolve_path(input_path, out_path, out_path_size);
    return;
  }

  app_copy_string(out_path, out_path_size, input_path == 0 ? "/" : input_path);
}

static void app_parse_invocation_args(const char *app_args, app_invocation_args_t *out_args) {
  size_t i = 0u;
  const char *raw = "";

  if (out_args == 0) {
    return;
  }

  out_args->argc = 0u;
  out_args->raw_args = "";
  out_args->args_buffer[0] = '\0';

  if (app_args == 0) {
    return;
  }

  raw = app_skip_spaces(app_args);
  out_args->raw_args = raw;
  app_copy_string(out_args->args_buffer, sizeof(out_args->args_buffer), raw);

  for (i = 0u; out_args->args_buffer[i] != '\0'; ++i) {
    if (app_char_is_space(out_args->args_buffer[i])) {
      out_args->args_buffer[i] = '\0';
    }
  }

  i = 0u;
  while (i < sizeof(out_args->args_buffer) && out_args->args_buffer[i] != '\0') {
    if (out_args->argc < APP_ARGV_MAX) {
      out_args->argv[out_args->argc++] = &out_args->args_buffer[i];
    }

    while (i < sizeof(out_args->args_buffer) && out_args->args_buffer[i] != '\0') {
      ++i;
    }
    while (i < sizeof(out_args->args_buffer) && out_args->args_buffer[i] == '\0') {
      ++i;
    }
  }
}

static void app_expand_invocation_tokens(const char *input,
                                         const app_invocation_args_t *args,
                                         char *out,
                                         size_t out_size) {
  size_t in_cursor = 0u;
  size_t out_cursor = 0u;

  if (out == 0 || out_size == 0u) {
    return;
  }

  out[0] = '\0';
  if (input == 0) {
    return;
  }

  while (input[in_cursor] != '\0' && out_cursor + 1u < out_size) {
    if (input[in_cursor] == '$') {
      if (input[in_cursor + 1u] >= '1' && input[in_cursor + 1u] <= '9') {
        size_t arg_index = (size_t)(input[in_cursor + 1u] - '1');
        if (args != 0 && arg_index < args->argc) {
          out_cursor = app_append_string(out, out_size, out_cursor, args->argv[arg_index]);
        }
        in_cursor += 2u;
        continue;
      }

      if (input[in_cursor + 1u] == 'A' &&
          input[in_cursor + 2u] == 'R' &&
          input[in_cursor + 3u] == 'G' &&
          input[in_cursor + 4u] == 'S') {
        if (args != 0 && args->raw_args != 0) {
          out_cursor = app_append_string(out, out_size, out_cursor, args->raw_args);
        }
        in_cursor += 5u;
        continue;
      }
    }

    out[out_cursor++] = input[in_cursor++];
    out[out_cursor] = '\0';
  }
}

static app_runtime_result_t app_sys_print(const app_runtime_context_t *context, const char *message) {
  if (!app_require_capability(context->subject_id, CAP_CONSOLE_WRITE)) {
    return APP_RUNTIME_ERR_CAPABILITY;
  }

  app_emit(context, message);
  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_sys_ls(const app_runtime_context_t *context, const char *path) {
  char output[APP_OUTPUT_MAX];
  char resolved[APP_TOKEN_MAX];
  size_t out_len = 0u;
  app_runtime_result_t access = APP_RUNTIME_OK;

  app_resolve_path(context, path, resolved, sizeof(resolved));

  access = app_require_storage_access(context, CAP_FS_READ, "ls", resolved);
  if (access != APP_RUNTIME_OK) {
    return access;
  }

  if (fs_list_dir(resolved, output, sizeof(output), &out_len) != FS_OK) {
    return APP_RUNTIME_ERR_STORAGE;
  }

  if (out_len == 0u) {
    app_emit(context, "(empty)\n");
  } else {
    app_emit(context, output);
  }

  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_sys_cat(const app_runtime_context_t *context, const char *path) {
  char output[APP_OUTPUT_MAX];
  char resolved[APP_TOKEN_MAX];
  size_t out_len = 0u;
  app_runtime_result_t access = APP_RUNTIME_OK;

  app_resolve_path(context, path, resolved, sizeof(resolved));

  access = app_require_storage_access(context, CAP_FS_READ, "cat", resolved);
  if (access != APP_RUNTIME_OK) {
    return access;
  }

  if (fs_read_file(resolved, output, sizeof(output), &out_len) != FS_OK) {
    return APP_RUNTIME_ERR_STORAGE;
  }

  app_emit(context, output);
  app_emit(context, "\n");
  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_sys_write_like(const app_runtime_context_t *context,
                                               const char *path,
                                               const char *content,
                                               int append) {
  char resolved[APP_TOKEN_MAX];
  app_runtime_result_t access = APP_RUNTIME_OK;

  app_resolve_path(context, path, resolved, sizeof(resolved));

  access = app_require_storage_access(context, CAP_FS_WRITE, append ? "append" : "write", resolved);
  if (access != APP_RUNTIME_OK) {
    return access;
  }

  if (fs_write_file(resolved, content, append) != FS_OK) {
    return APP_RUNTIME_ERR_STORAGE;
  }

  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_sys_mkdir(const app_runtime_context_t *context, const char *path) {
  char resolved[APP_TOKEN_MAX];
  app_runtime_result_t access = APP_RUNTIME_OK;

  app_resolve_path(context, path, resolved, sizeof(resolved));

  access = app_require_storage_access(context, CAP_FS_WRITE, "mkdir", resolved);
  if (access != APP_RUNTIME_OK) {
    return access;
  }

  if (fs_mkdir(resolved) != FS_OK) {
    return APP_RUNTIME_ERR_STORAGE;
  }

  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_sys_cd(const app_runtime_context_t *context, const char *path) {
  char resolved[APP_TOKEN_MAX];

  if (context == 0 || context->change_directory == 0) {
    return APP_RUNTIME_ERR_DENIED;
  }

  app_resolve_path(context, path, resolved, sizeof(resolved));
  if (!context->change_directory(resolved)) {
    return APP_RUNTIME_ERR_NOT_FOUND;
  }

  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_sys_apps(const app_runtime_context_t *context) {
  char output[APP_OUTPUT_MAX];
  size_t len = 0u;

  if (!app_require_capability(context->subject_id, CAP_CONSOLE_WRITE)) {
    return APP_RUNTIME_ERR_CAPABILITY;
  }

  len = app_runtime_list(output, sizeof(output));
  if (len == 0u) {
    app_emit(context, "(no apps)\n");
  } else {
    app_emit(context, output);
  }

  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_sys_storage(const app_runtime_context_t *context) {
  char output[APP_OUTPUT_MAX];
  size_t cursor = 0u;

  if (!app_require_capability(context->subject_id, CAP_CONSOLE_WRITE)) {
    return APP_RUNTIME_ERR_CAPABILITY;
  }

  cursor = app_append_string(output, sizeof(output), cursor, "storage backend=");
  cursor = app_append_string(output, sizeof(output), cursor, storage_hal_backend_name());
  cursor = app_append_string(output, sizeof(output), cursor, " blocks=");
  cursor = app_append_u32_decimal(output, sizeof(output), cursor, storage_hal_block_count());
  cursor = app_append_string(output, sizeof(output), cursor, " block_size=");
  cursor = app_append_u32_decimal(output, sizeof(output), cursor, storage_hal_block_size());
  cursor = app_append_string(output, sizeof(output), cursor, "\n");

  if (cursor > 0u) {
    app_emit(context, output);
  }

  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_sys_uselib(const app_runtime_context_t *context, const char *handle_arg);
static app_runtime_result_t app_sys_releaselib(const app_runtime_context_t *context, const char *handle_arg);

static app_runtime_result_t app_sys_libs(const app_runtime_context_t *context, const char *args) {
  char output[APP_OUTPUT_MAX];
  size_t len = 0u;

  if (!app_require_capability(context->subject_id, CAP_CONSOLE_WRITE)) {
    return APP_RUNTIME_ERR_CAPABILITY;
  }

  if (args != 0 && app_string_equals(args, "loaded")) {
    if (context->list_loaded_libraries == 0) {
      app_emit(context, "(no loaded libraries)\n");
      return APP_RUNTIME_OK;
    }

    len = context->list_loaded_libraries(output, sizeof(output));
    if (len > 0u) {
      app_emit(context, output);
    } else {
      app_emit(context, "(no loaded libraries)\n");
    }
    return APP_RUNTIME_OK;
  }

  if (args != 0 && app_string_starts_with(args, "use ")) {
    return app_sys_uselib(context, app_skip_spaces(args + 4u));
  }

  if (args != 0 && app_string_starts_with(args, "release ")) {
    return app_sys_releaselib(context, app_skip_spaces(args + 8u));
  }

  len = app_runtime_list_libraries(output, sizeof(output));
  if (len == 0u) {
    app_emit(context, "(no libraries)\n");
  } else {
    app_emit(context, output);
  }

  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_sys_loadlib(const app_runtime_context_t *context, const char *library_name) {
  app_runtime_library_info_t library_info;
  unsigned int handle = 0u;
  char handle_text[24];
  size_t cursor = 0u;
  app_runtime_result_t result = APP_RUNTIME_OK;

  if (library_name == 0 || library_name[0] == '\0') {
    return APP_RUNTIME_ERR_INVALID_ARG;
  }

  result = app_runtime_load_library(library_name, context, &library_info);
  if (result != APP_RUNTIME_OK) {
    return result;
  }

  if (context->register_loaded_library != 0) {
    if (!context->register_loaded_library(
            library_info.resolved_path,
            library_info.program_len,
            context->actor_name,
            &handle)) {
      return APP_RUNTIME_ERR_DENIED;
    }
  }

  app_emit(context, "[lib] loaded ");
  app_emit(context, library_info.resolved_path);
  if (handle != 0u) {
    cursor = app_append_string(handle_text, sizeof(handle_text), cursor, " handle=");
    cursor = app_append_u32_decimal(handle_text, sizeof(handle_text), cursor, handle);
    app_emit(context, handle_text);
  }
  app_emit(context, "\n");
  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_sys_unloadlib(const app_runtime_context_t *context, const char *handle_arg) {
  char path[APP_RUNTIME_LIBRARY_PATH_MAX];
  char handle_text[24];
  unsigned int handle = 0u;
  unsigned int ref_count = 0u;
  int ok = 0;
  size_t cursor = 0u;

  if (context == 0 || context->unregister_loaded_library == 0) {
    return APP_RUNTIME_ERR_DENIED;
  }

  handle = app_parse_u32(handle_arg, &ok);
  if (!ok) {
    return APP_RUNTIME_ERR_INVALID_ARG;
  }

  if (context->get_loaded_library_ref_count != 0) {
    if (!context->get_loaded_library_ref_count(handle, &ref_count)) {
      return APP_RUNTIME_ERR_NOT_FOUND;
    }
    if (ref_count > 0u) {
      return APP_RUNTIME_ERR_IN_USE;
    }
  }

  if (!context->unregister_loaded_library(handle, path, sizeof(path))) {
    return APP_RUNTIME_ERR_NOT_FOUND;
  }

  app_emit(context, "[lib] unloaded handle=");
  cursor = app_append_u32_decimal(handle_text, sizeof(handle_text), cursor, handle);
  if (cursor > 0u) {
    app_emit(context, handle_text);
  }
  if (path[0] != '\0') {
    app_emit(context, " path=");
    app_emit(context, path);
  }
  app_emit(context, "\n");
  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_sys_uselib(const app_runtime_context_t *context, const char *handle_arg) {
  unsigned int handle = 0u;
  unsigned int ref_count = 0u;
  int ok = 0;
  char text[24];
  size_t cursor = 0u;

  if (context == 0 || context->acquire_loaded_library == 0) {
    return APP_RUNTIME_ERR_DENIED;
  }

  handle = app_parse_u32(handle_arg, &ok);
  if (!ok) {
    return APP_RUNTIME_ERR_INVALID_ARG;
  }

  if (!context->acquire_loaded_library(handle, &ref_count)) {
    return APP_RUNTIME_ERR_NOT_FOUND;
  }

  app_emit(context, "[lib] use handle=");
  cursor = app_append_u32_decimal(text, sizeof(text), cursor, handle);
  if (cursor > 0u) {
    app_emit(context, text);
  }
  cursor = 0u;
  cursor = app_append_string(text, sizeof(text), cursor, " refs=");
  cursor = app_append_u32_decimal(text, sizeof(text), cursor, ref_count);
  app_emit(context, text);
  app_emit(context, "\n");
  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_sys_releaselib(const app_runtime_context_t *context, const char *handle_arg) {
  unsigned int handle = 0u;
  unsigned int ref_count = 0u;
  int ok = 0;
  char text[24];
  size_t cursor = 0u;

  if (context == 0 || context->release_loaded_library == 0) {
    return APP_RUNTIME_ERR_DENIED;
  }

  handle = app_parse_u32(handle_arg, &ok);
  if (!ok) {
    return APP_RUNTIME_ERR_INVALID_ARG;
  }

  if (!context->release_loaded_library(handle, &ref_count)) {
    return APP_RUNTIME_ERR_NOT_FOUND;
  }

  app_emit(context, "[lib] release handle=");
  cursor = app_append_u32_decimal(text, sizeof(text), cursor, handle);
  if (cursor > 0u) {
    app_emit(context, text);
  }
  cursor = 0u;
  cursor = app_append_string(text, sizeof(text), cursor, " refs=");
  cursor = app_append_u32_decimal(text, sizeof(text), cursor, ref_count);
  app_emit(context, text);
  app_emit(context, "\n");
  return APP_RUNTIME_OK;
}

static int app_env_read_quoted(const char *src,
                               char *out_value,
                               size_t out_value_size,
                               const char **out_next) {
  size_t i = 0u;
  size_t cursor = 0u;

  if (src == 0 || src[0] != '"' || out_value == 0 || out_value_size == 0u) {
    return 0;
  }

  i = 1u;
  out_value[0] = '\0';
  while (src[i] != '\0') {
    char ch = src[i];

    if (ch == '"') {
      if (out_next != 0) {
        *out_next = src + i + 1u;
      }
      out_value[cursor] = '\0';
      return 1;
    }

    if ((ch == '\\' || ch == '/') && (src[i + 1u] == '"' || src[i + 1u] == '\\' || src[i + 1u] == '/')) {
      ch = src[i + 1u];
      i += 2u;
    } else {
      ++i;
    }

    if (cursor + 1u < out_value_size) {
      out_value[cursor++] = ch;
      out_value[cursor] = '\0';
    }
  }

  return 0;
}

static void app_env_copy_unquoted_token(const char *src,
                                        char *out_value,
                                        size_t out_value_size,
                                        const char **out_next) {
  size_t i = 0u;

  if (out_value == 0 || out_value_size == 0u) {
    if (out_next != 0) {
      *out_next = src;
    }
    return;
  }

  while (src[i] != '\0' && !app_char_is_space(src[i]) && i + 1u < out_value_size) {
    out_value[i] = src[i];
    ++i;
  }
  out_value[i] = '\0';

  if (out_next != 0) {
    *out_next = src + i;
  }
}

static void app_env_copy_trimmed_tail(const char *src, char *out_value, size_t out_value_size) {
  size_t len = 0u;
  size_t end = 0u;
  size_t i = 0u;

  if (out_value == 0 || out_value_size == 0u) {
    return;
  }

  out_value[0] = '\0';
  if (src == 0) {
    return;
  }

  src = app_skip_spaces(src);
  len = app_string_len(src);
  end = len;
  while (end > 0u && app_char_is_space(src[end - 1u])) {
    --end;
  }

  while (i < end && i + 1u < out_value_size) {
    out_value[i] = src[i];
    ++i;
  }
  out_value[i] = '\0';
}

static int app_env_extract_named_arg(const char *args,
                                     const char *name,
                                     char *out_value,
                                     size_t out_value_size) {
  const char *cursor = app_skip_spaces(args);

  if (args == 0 || name == 0 || out_value == 0 || out_value_size == 0u) {
    return 0;
  }

  out_value[0] = '\0';
  while (cursor[0] != '\0') {
    char token_name[APP_TOKEN_MAX];
    size_t token_len = 0u;
    const char *value_start = 0;
    const char *next = 0;

    while (cursor[token_len] != '\0' && cursor[token_len] != '=' && !app_char_is_space(cursor[token_len])) {
      if (token_len + 1u < sizeof(token_name)) {
        token_name[token_len] = cursor[token_len];
      }
      ++token_len;
    }
    if (token_len + 1u < sizeof(token_name)) {
      token_name[token_len] = '\0';
    } else {
      token_name[sizeof(token_name) - 1u] = '\0';
    }

    if (cursor[token_len] == '=') {
      value_start = cursor + token_len + 1u;
      if (value_start[0] == '"') {
        if (!app_env_read_quoted(value_start, out_value, out_value_size, &next)) {
          return 0;
        }
      } else {
        app_env_copy_unquoted_token(value_start, out_value, out_value_size, &next);
      }

      if (app_string_equals(token_name, name)) {
        return 1;
      }

      cursor = app_skip_spaces(next);
      continue;
    }

    while (cursor[token_len] != '\0' && !app_char_is_space(cursor[token_len])) {
      ++token_len;
    }
    cursor = app_skip_spaces(cursor + token_len);
  }

  return 0;
}

static int app_env_find_eq_before_space(const char *args, size_t *out_pos) {
  size_t i = 0u;

  if (args == 0 || out_pos == 0) {
    return 0;
  }

  while (args[i] != '\0') {
    if (args[i] == '=') {
      *out_pos = i;
      return 1;
    }
    if (app_char_is_space(args[i])) {
      return 0;
    }
    ++i;
  }

  return 0;
}

static app_runtime_result_t app_sys_env(const app_runtime_context_t *context, const char *args) {
  char key[APP_TOKEN_MAX];
  char value[APP_LINE_MAX];
  char set_value[APP_LINE_MAX];
  char named_key[APP_TOKEN_MAX];
  char named_value[APP_LINE_MAX];
  const char *trimmed_args = 0;
  const char *next = 0;
  const char *rest = 0;
  const char *equals = 0;
  size_t eq_pos = 0u;

  if (context == 0) {
    return APP_RUNTIME_ERR_INVALID_ARG;
  }

  trimmed_args = app_skip_spaces(args == 0 ? "" : args);
  if (trimmed_args[0] == '\0') {
    size_t listed = 0u;
    if (context->list_env == 0) {
      return APP_RUNTIME_ERR_DENIED;
    }

    listed = context->list_env(value, sizeof(value));
    if (listed == 0u) {
      app_emit(context, "(empty)\n");
      return APP_RUNTIME_OK;
    }

    app_emit(context, value);
    return APP_RUNTIME_OK;
  }

  if (app_env_extract_named_arg(trimmed_args, "key", named_key, sizeof(named_key))) {
    if (app_env_extract_named_arg(trimmed_args, "value", named_value, sizeof(named_value))) {
      if (context->set_env == 0 || named_key[0] == '\0') {
        return APP_RUNTIME_ERR_DENIED;
      }
      return context->set_env(named_key, named_value) ? APP_RUNTIME_OK : APP_RUNTIME_ERR_DENIED;
    }

    if (context->get_env == 0) {
      return APP_RUNTIME_ERR_DENIED;
    }
    if (!context->get_env(named_key, value, sizeof(value))) {
      return APP_RUNTIME_ERR_NOT_FOUND;
    }
    app_emit(context, value);
    app_emit(context, "\n");
    return APP_RUNTIME_OK;
  }

  if (app_env_find_eq_before_space(trimmed_args, &eq_pos)) {
    size_t i = 0u;

    if (context->set_env == 0 || eq_pos == 0u || eq_pos >= sizeof(key)) {
      return APP_RUNTIME_ERR_DENIED;
    }

    for (i = 0u; i < eq_pos && i + 1u < sizeof(key); ++i) {
      key[i] = trimmed_args[i];
    }
    key[i] = '\0';

    rest = trimmed_args + eq_pos + 1u;
    if (rest[0] == '"') {
      if (!app_env_read_quoted(rest, set_value, sizeof(set_value), &next)) {
        return APP_RUNTIME_ERR_FORMAT;
      }
      if (app_skip_spaces(next)[0] != '\0') {
        return APP_RUNTIME_ERR_FORMAT;
      }
    } else {
      app_env_copy_trimmed_tail(rest, set_value, sizeof(set_value));
    }

    return context->set_env(key, set_value) ? APP_RUNTIME_OK : APP_RUNTIME_ERR_DENIED;
  }

  app_copy_until_space(key, sizeof(key), trimmed_args, &rest);
  rest = app_skip_spaces(rest);
  equals = app_find_char(key, '=');

  if (equals != 0) {
    size_t key_len = (size_t)(equals - key);
    size_t i = 0u;

    if (context->set_env == 0 || key_len == 0u) {
      return APP_RUNTIME_ERR_DENIED;
    }

    app_copy_string(set_value, sizeof(set_value), equals + 1u);

    for (i = key_len; i < sizeof(key); ++i) {
      key[i] = '\0';
    }
    if (!context->set_env(key, set_value)) {
      return APP_RUNTIME_ERR_DENIED;
    }
    return APP_RUNTIME_OK;
  }

  if (rest[0] != '\0') {
    if (context->set_env == 0) {
      return APP_RUNTIME_ERR_DENIED;
    }

    if (rest[0] == '"') {
      if (!app_env_read_quoted(rest, set_value, sizeof(set_value), &next)) {
        return APP_RUNTIME_ERR_FORMAT;
      }
      if (app_skip_spaces(next)[0] != '\0') {
        return APP_RUNTIME_ERR_FORMAT;
      }
      if (!context->set_env(key, set_value)) {
        return APP_RUNTIME_ERR_DENIED;
      }
      return APP_RUNTIME_OK;
    }

    if (!context->set_env(key, rest)) {
      return APP_RUNTIME_ERR_DENIED;
    }
    return APP_RUNTIME_OK;
  }

  if (context->get_env == 0) {
    return APP_RUNTIME_ERR_DENIED;
  }

  if (!context->get_env(key, value, sizeof(value))) {
    return APP_RUNTIME_ERR_NOT_FOUND;
  }

  app_emit(context, value);
  app_emit(context, "\n");
  return APP_RUNTIME_OK;
}

static int app_path_is_library(const char *path) {
  if (path == 0) {
    return 0;
  }

  return app_string_starts_with(path, "/lib/") || app_string_starts_with(path, "\\lib\\") ||
         app_string_starts_with(path, "lib/") || app_string_starts_with(path, "lib\\");
}

static void app_build_library_path(char *out_path, size_t out_path_size, const char *library_name) {
  size_t cursor = 0u;

  if (out_path == 0 || out_path_size == 0u) {
    return;
  }

  out_path[0] = '\0';
  if (library_name == 0 || library_name[0] == '\0') {
    return;
  }

  if (app_path_is_library(library_name)) {
    if (library_name[0] == '/' || library_name[0] == '\\') {
      app_copy_string(out_path, out_path_size, library_name + 1u);
    } else {
      app_copy_string(out_path, out_path_size, library_name);
    }
    return;
  }

  cursor = app_append_string(out_path, out_path_size, cursor, "lib/");
  cursor = app_append_string(out_path, out_path_size, cursor, library_name);
  if (!app_string_ends_with(library_name, ".elf")) {
    (void)app_append_string(out_path, out_path_size, cursor, ".elf");
  }
}

size_t app_runtime_list_libraries(char *out_buffer, size_t out_buffer_size) {
  size_t out_len = 0u;

  if (out_buffer == 0 || out_buffer_size == 0u) {
    return 0u;
  }

  out_buffer[0] = '\0';
  if (fs_list_dir("/lib", out_buffer, out_buffer_size, &out_len) != FS_OK) {
    return 0u;
  }

  return out_len;
}

app_runtime_result_t app_runtime_load_library(const char *library_name,
                                              const app_runtime_context_t *context,
                                              app_runtime_library_info_t *out_library) {
  uint8_t elf_data[APP_FILE_MAX];
  size_t elf_len = 0u;
  const uint8_t *program = 0;
  size_t program_len = 0u;
  char library_path[APP_TOKEN_MAX];
  char display_path[APP_TOKEN_MAX];
  app_runtime_result_t access = APP_RUNTIME_OK;
  fs_result_t fs_result = FS_OK;

  if (library_name == 0 || library_name[0] == '\0' || context == 0 || out_library == 0) {
    return APP_RUNTIME_ERR_INVALID_ARG;
  }

  app_build_library_path(library_path, sizeof(library_path), library_name);
  if (!app_path_is_library(library_path)) {
    return APP_RUNTIME_ERR_LIBRARY;
  }

  display_path[0] = '/';
  display_path[1] = '\0';
  (void)app_append_string(display_path, sizeof(display_path), 1u, library_path);

  access = app_require_storage_access(context, CAP_FS_READ, "loadlib", display_path);
  if (access != APP_RUNTIME_OK) {
    return access;
  }

  fs_result = fs_read_file_bytes(library_path, elf_data, sizeof(elf_data), &elf_len);
  if (fs_result == FS_ERR_NOT_FOUND) {
    return APP_RUNTIME_ERR_NOT_FOUND;
  }
  if (fs_result != FS_OK) {
    return APP_RUNTIME_ERR_STORAGE;
  }

  if (app_parse_elf_program(elf_data, elf_len, &program, &program_len) != APP_RUNTIME_OK) {
    return APP_RUNTIME_ERR_FORMAT;
  }

  app_copy_string(out_library->resolved_path, sizeof(out_library->resolved_path), display_path);
  out_library->program_len = program_len;
  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_parse_elf_program(const uint8_t *elf_data,
                                                  size_t elf_len,
                                                  const uint8_t **out_program,
                                                  size_t *out_program_len) {
  uint32_t e_phoff = 0u;
  uint16_t e_phentsize = 0u;
  uint16_t e_phnum = 0u;
  uint16_t i = 0u;

  if (elf_data == 0 || out_program == 0 || out_program_len == 0) {
    return APP_RUNTIME_ERR_INVALID_ARG;
  }

  if (elf_len < 52u) {
    return APP_RUNTIME_ERR_FORMAT;
  }

  if (!(elf_data[0] == 0x7Fu && elf_data[1] == 'E' && elf_data[2] == 'L' && elf_data[3] == 'F')) {
    return APP_RUNTIME_ERR_FORMAT;
  }

  if (elf_data[4] != 1u || elf_data[5] != 1u || app_read_u16(elf_data, 18u) != 3u) {
    return APP_RUNTIME_ERR_FORMAT;
  }

  e_phoff = app_read_u32(elf_data, 28u);
  e_phentsize = app_read_u16(elf_data, 42u);
  e_phnum = app_read_u16(elf_data, 44u);

  if (e_phoff >= elf_len || e_phentsize < 32u || e_phnum == 0u) {
    return APP_RUNTIME_ERR_FORMAT;
  }

  for (i = 0u; i < e_phnum; ++i) {
    size_t ph_off = (size_t)e_phoff + ((size_t)i * (size_t)e_phentsize);
    uint32_t p_type = 0u;
    uint32_t p_offset = 0u;
    uint32_t p_filesz = 0u;

    if (ph_off + 32u > elf_len) {
      return APP_RUNTIME_ERR_FORMAT;
    }

    p_type = app_read_u32(elf_data, ph_off + 0u);
    p_offset = app_read_u32(elf_data, ph_off + 4u);
    p_filesz = app_read_u32(elf_data, ph_off + 16u);

    if (p_type != 1u || p_filesz == 0u) {
      continue;
    }

    if ((size_t)p_offset + (size_t)p_filesz > elf_len) {
      return APP_RUNTIME_ERR_FORMAT;
    }

    *out_program = &elf_data[p_offset];
    *out_program_len = p_filesz;
    return APP_RUNTIME_OK;
  }

  return APP_RUNTIME_ERR_FORMAT;
}

static app_runtime_result_t app_execute_script(const uint8_t *script,
                                               size_t script_len,
                                               const app_runtime_context_t *context,
                                               const app_invocation_args_t *args) {
  size_t cursor = 0u;

  while (cursor < script_len) {
    char line[APP_LINE_MAX];
    char expanded_line[APP_LINE_MAX];
    size_t line_len = 0u;
    const char *line_cursor = 0;
    const char *rest = 0;
    char command[APP_TOKEN_MAX];

    while (cursor < script_len && script[cursor] != '\n' && line_len + 1u < sizeof(line)) {
      line[line_len++] = (char)script[cursor++];
    }
    line[line_len] = '\0';

    if (cursor < script_len && script[cursor] == '\n') {
      ++cursor;
    }

    app_expand_invocation_tokens(line, args, expanded_line, sizeof(expanded_line));
    line_cursor = app_skip_spaces(expanded_line);
    if (line_cursor[0] == '\0' || line_cursor[0] == '#') {
      continue;
    }

    app_copy_until_space(command, sizeof(command), line_cursor, &rest);
    rest = app_skip_spaces(rest);

    if (app_string_equals(command, "print")) {
      app_runtime_result_t result = app_sys_print(context, rest);
      if (result != APP_RUNTIME_OK) {
        return result;
      }
      continue;
    }

    if (app_string_equals(command, "ls")) {
      app_runtime_result_t result = app_sys_ls(context, rest[0] == '\0' ? "." : rest);
      if (result != APP_RUNTIME_OK) {
        return result;
      }
      continue;
    }

    if (app_string_equals(command, "cat")) {
      if (rest[0] == '\0') {
        return APP_RUNTIME_ERR_FORMAT;
      }
      {
        app_runtime_result_t result = app_sys_cat(context, rest);
        if (result != APP_RUNTIME_OK) {
          return result;
        }
      }
      continue;
    }

    if (app_string_equals(command, "write") || app_string_equals(command, "append")) {
      char arg1[APP_TOKEN_MAX];
      char arg2[APP_LINE_MAX];
      const char *next = 0;
      app_runtime_result_t result = APP_RUNTIME_OK;

      app_copy_until_space(arg1, sizeof(arg1), rest, &next);
      next = app_skip_spaces(next);
      app_copy_string(arg2, sizeof(arg2), next);

      if (arg1[0] == '\0' || arg2[0] == '\0') {
        return APP_RUNTIME_ERR_FORMAT;
      }

      result = app_sys_write_like(context, arg1, arg2, app_string_equals(command, "append"));
      if (result != APP_RUNTIME_OK) {
        return result;
      }
      continue;
    }

    if (app_string_equals(command, "mkdir")) {
      if (rest[0] == '\0') {
        return APP_RUNTIME_ERR_FORMAT;
      }
      {
        app_runtime_result_t result = app_sys_mkdir(context, rest);
        if (result != APP_RUNTIME_OK) {
          return result;
        }
      }
      continue;
    }

    if (app_string_equals(command, "cd")) {
      if (rest[0] == '\0') {
        return APP_RUNTIME_ERR_FORMAT;
      }
      {
        app_runtime_result_t result = app_sys_cd(context, rest);
        if (result != APP_RUNTIME_OK) {
          return result;
        }
      }
      continue;
    }

    if (app_string_equals(command, "apps")) {
      app_runtime_result_t result = app_sys_apps(context);
      if (result != APP_RUNTIME_OK) {
        return result;
      }
      continue;
    }

    if (app_string_equals(command, "libs")) {
      app_runtime_result_t result = app_sys_libs(context, rest);
      if (result != APP_RUNTIME_OK) {
        return result;
      }
      continue;
    }

    if (app_string_equals(command, "storage")) {
      app_runtime_result_t result = app_sys_storage(context);
      if (result != APP_RUNTIME_OK) {
        return result;
      }
      continue;
    }

    if (app_string_equals(command, "env")) {
      app_runtime_result_t result = app_sys_env(context, rest);
      if (result != APP_RUNTIME_OK) {
        return result;
      }
      continue;
    }

    if (app_string_equals(command, "loadlib")) {
      app_runtime_result_t result = app_sys_loadlib(context, rest);
      if (result != APP_RUNTIME_OK) {
        return result;
      }
      continue;
    }

    if (app_string_equals(command, "unloadlib")) {
      app_runtime_result_t result = app_sys_unloadlib(context, rest);
      if (result != APP_RUNTIME_OK) {
        return result;
      }
      continue;
    }

    if (app_string_equals(command, "uselib")) {
      app_runtime_result_t result = app_sys_uselib(context, rest);
      if (result != APP_RUNTIME_OK) {
        return result;
      }
      continue;
    }

    if (app_string_equals(command, "releaselib")) {
      app_runtime_result_t result = app_sys_releaselib(context, rest);
      if (result != APP_RUNTIME_OK) {
        return result;
      }
      continue;
    }

    return APP_RUNTIME_ERR_FORMAT;
  }

  return APP_RUNTIME_OK;
}

static void app_build_path(char *out_path, size_t out_path_size, const char *base_dir, const char *app_name) {
  size_t cursor = 0u;

  if (out_path == 0 || out_path_size == 0u) {
    return;
  }

  out_path[0] = '\0';
  cursor = app_append_string(out_path, out_path_size, cursor, base_dir);
  if (cursor + 1u < out_path_size) {
    out_path[cursor++] = '/';
    out_path[cursor] = '\0';
  }
  cursor = app_append_string(out_path, out_path_size, cursor, app_name);
  (void)app_append_string(out_path, out_path_size, cursor, ".elf");
}

size_t app_runtime_list(char *out_buffer, size_t out_buffer_size) {
  char os_apps[APP_OUTPUT_MAX];
  char user_apps[APP_OUTPUT_MAX];
  size_t os_len = 0u;
  size_t user_len = 0u;
  size_t cursor = 0u;

  if (out_buffer == 0 || out_buffer_size == 0u) {
    return 0u;
  }

  out_buffer[0] = '\0';

  if (fs_list_dir("/os", os_apps, sizeof(os_apps), &os_len) == FS_OK) {
    cursor = app_append_string(out_buffer, out_buffer_size, cursor, "os/\n");
    cursor = app_append_string(out_buffer, out_buffer_size, cursor, os_apps);
  }

  if (fs_list_dir("/apps", user_apps, sizeof(user_apps), &user_len) == FS_OK) {
    cursor = app_append_string(out_buffer, out_buffer_size, cursor, "apps/\n");
    cursor = app_append_string(out_buffer, out_buffer_size, cursor, user_apps);
  }

  return cursor;
}

app_runtime_result_t app_runtime_run(const char *app_name,
                                     const char *app_args,
                                     const app_runtime_context_t *context) {
  uint8_t elf_data[APP_FILE_MAX];
  size_t elf_len = 0u;
  const uint8_t *program = 0;
  size_t program_len = 0u;
  char path[APP_TOKEN_MAX];
  fs_result_t fs_result = FS_OK;
  app_invocation_args_t args;

  if (app_name == 0 || context == 0) {
    return APP_RUNTIME_ERR_INVALID_ARG;
  }

  if (app_name[0] == '\0') {
    return APP_RUNTIME_ERR_INVALID_ARG;
  }

  if (app_path_is_library(app_name)) {
    return APP_RUNTIME_ERR_LIBRARY;
  }

  app_parse_invocation_args(app_args, &args);

  if (app_name[0] == '/' || app_name[0] == '\\') {
    app_copy_string(path, sizeof(path), app_name + 1u);
    if (app_path_is_library(path)) {
      return APP_RUNTIME_ERR_LIBRARY;
    }
    fs_result = fs_read_file_bytes(path, elf_data, sizeof(elf_data), &elf_len);
  } else {
    app_build_path(path, sizeof(path), "os", app_name);
    fs_result = fs_read_file_bytes(path, elf_data, sizeof(elf_data), &elf_len);

    if (fs_result != FS_OK) {
      app_build_path(path, sizeof(path), "apps", app_name);
      fs_result = fs_read_file_bytes(path, elf_data, sizeof(elf_data), &elf_len);
    }
  }

  if (fs_result == FS_ERR_NOT_FOUND) {
    return APP_RUNTIME_ERR_NOT_FOUND;
  }
  if (fs_result != FS_OK) {
    return APP_RUNTIME_ERR_STORAGE;
  }

  if (app_parse_elf_program(elf_data, elf_len, &program, &program_len) != APP_RUNTIME_OK) {
    return APP_RUNTIME_ERR_FORMAT;
  }

  return app_execute_script(program, program_len, context, &args);
}
