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
      app_runtime_result_t result = app_sys_ls(context, rest[0] == '\0' ? "/" : rest);
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

    if (app_string_equals(command, "storage")) {
      app_runtime_result_t result = app_sys_storage(context);
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

  app_parse_invocation_args(app_args, &args);

  if (app_name[0] == '/' || app_name[0] == '\\') {
    app_copy_string(path, sizeof(path), app_name + 1u);
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
