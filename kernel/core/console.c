/**
 * @file console.c
 * @brief Interactive kernel console (shell) and command dispatcher.
 *
 * Purpose:
 *   Implements the kernel's interactive command-line shell.  Reads
 *   characters from the serial port, assembles command lines, parses
 *   built-in commands (exit, ls, cat, write, etc.), and dispatches
 *   user-space application binaries through the process subsystem.  Also
 *   manages per-session environment variables and working directory.
 *
 * Interactions:
 *   - serial_hal.c / video_hal.c: used for character I/O (input from serial,
 *     output to both serial and VGA).
 *   - cap_table.c: the console's own subject ID is granted initial
 *     capabilities during console_init.
 *   - process.c: user-space commands are loaded and executed
 *     through the process execution module.
 *   - fs_service.c: filesystem operations (list, read, write, mkdir)
 *     are invoked for built-in file commands.
 *   - session_manager.c: provides the per-session console_context_t
 *     that holds shell state (cwd, env, line buffer).
 *   - event_bus.c: console commands may emit audit events.
 *
 * Launched by:
 *   console_run() is called from kmain() after all subsystems are
 *   initialized.  Not a standalone process; compiled into the kernel
 *   image.
 */

#include "console.h"

#include <stdint.h>

#include "../arch/x86/debug_exit.h"
#include "../cap/cap_table.h"
#include "../event/event_bus.h"
#include "../fs/fs_service.h"
#include "../hal/serial_hal.h"
#include "../hal/video_hal.h"
#include "../user/process.h"
#include "session_manager.h"

#define CONSOLE_OUTPUT_MAX 512

static console_context_t *g_console_ctx = 0;

#define console_subject_id (g_console_ctx->subject_id)
#define console_line (g_console_ctx->line)
#define console_line_len (g_console_ctx->line_len)
#define console_pending_line (g_console_ctx->pending_line)
#define console_pending_line_len (g_console_ctx->pending_line_len)
#define console_history (g_console_ctx->history)
#define console_history_count (g_console_ctx->history_count)
#define console_history_next (g_console_ctx->history_next)
#define console_history_browse_index (g_console_ctx->history_browse_index)
#define console_screen_history (g_console_ctx->screen_history)
#define console_screen_history_len (g_console_ctx->screen_history_len)
#define console_next_correlation_id (g_console_ctx->next_correlation_id)
#define console_cwd (g_console_ctx->cwd)
#define console_env_entries (g_console_ctx->env_entries)
#define console_loaded_libs (g_console_ctx->loaded_libs)
#define console_next_loaded_lib_handle (g_console_ctx->next_loaded_lib_handle)
#define console_auth_cache (g_console_ctx->auth_cache)

static int g_console_restoring_history = 0;

static int console_directory_exists(const char *absolute_path);

static void console_idle_wait(void) {
  volatile int spin = 0;
  spin++;
}

static int string_equals(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (*a != *b) {
      return 0;
    }
    ++a;
    ++b;
  }

  return *a == *b;
}

static int string_starts_with(const char *value, const char *prefix) {
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

static size_t string_len(const char *value) {
  size_t len = 0u;
  if (value == 0) {
    return 0u;
  }
  while (value[len] != '\0') {
    ++len;
  }
  return len;
}

static int char_is_space(char value) {
  return value == ' ' || value == '\t';
}

static const char *skip_spaces(const char *value) {
  while (*value != '\0' && char_is_space(*value)) {
    ++value;
  }
  return value;
}

static void copy_until_space(char *dst, size_t dst_size, const char *src, const char **out_next) {
  size_t i = 0u;

  if (dst_size == 0u) {
    if (out_next != 0) {
      *out_next = src;
    }
    return;
  }

  while (src[i] != '\0' && !char_is_space(src[i]) && i + 1u < dst_size) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';

  if (out_next != 0) {
    *out_next = src + i;
  }
}

static void copy_string(char *dst, size_t dst_size, const char *src) {
  size_t i = 0u;

  if (dst_size == 0u) {
    return;
  }

  while (src[i] != '\0' && i + 1u < dst_size) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

static size_t append_string(char *dst, size_t dst_size, size_t cursor, const char *src) {
  size_t i = 0u;

  while (src[i] != '\0' && cursor + 1u < dst_size) {
    dst[cursor++] = src[i++];
  }

  if (cursor < dst_size) {
    dst[cursor] = '\0';
  }
  return cursor;
}

static size_t append_u32_decimal(char *dst, size_t dst_size, size_t cursor, unsigned int value) {
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

  for (i = 0u; i < count && cursor + 1u < dst_size; ++i) {
    dst[cursor++] = digits[count - i - 1u];
  }

  if (cursor < dst_size) {
    dst[cursor] = '\0';
  }
  return cursor;
}

static void console_env_reset(void) {
  size_t i = 0u;
  for (i = 0u; i < CONSOLE_ENV_MAX; ++i) {
    console_env_entries[i].used = 0;
    console_env_entries[i].key[0] = '\0';
    console_env_entries[i].value[0] = '\0';
  }
}

static int console_env_find_slot(const char *key) {
  size_t i = 0u;
  for (i = 0u; i < CONSOLE_ENV_MAX; ++i) {
    if (console_env_entries[i].used && string_equals(console_env_entries[i].key, key)) {
      return (int)i;
    }
  }
  return -1;
}

static int console_env_find_free_slot(void) {
  size_t i = 0u;
  for (i = 0u; i < CONSOLE_ENV_MAX; ++i) {
    if (!console_env_entries[i].used) {
      return (int)i;
    }
  }
  return -1;
}

static int console_env_set(const char *key, const char *value) {
  int slot = -1;

  if (key == 0 || value == 0 || key[0] == '\0') {
    return 0;
  }

  slot = console_env_find_slot(key);
  if (slot < 0) {
    slot = console_env_find_free_slot();
  }
  if (slot < 0) {
    return 0;
  }

  console_env_entries[slot].used = 1;
  copy_string(console_env_entries[slot].key, sizeof(console_env_entries[slot].key), key);
  copy_string(console_env_entries[slot].value, sizeof(console_env_entries[slot].value), value);
  return 1;
}

static int console_env_get(const char *key, char *out_value, size_t out_value_size) {
  int slot = 0;

  if (key == 0 || out_value == 0 || out_value_size == 0u) {
    return 0;
  }

  slot = console_env_find_slot(key);
  if (slot < 0) {
    return 0;
  }

  copy_string(out_value, out_value_size, console_env_entries[slot].value);
  return 1;
}

static size_t console_env_list(char *out_buffer, size_t out_buffer_size) {
  size_t i = 0u;
  size_t cursor = 0u;

  if (out_buffer == 0 || out_buffer_size == 0u) {
    return 0u;
  }

  out_buffer[0] = '\0';
  for (i = 0u; i < CONSOLE_ENV_MAX; ++i) {
    if (!console_env_entries[i].used) {
      continue;
    }

    cursor = append_string(out_buffer, out_buffer_size, cursor, console_env_entries[i].key);
    cursor = append_string(out_buffer, out_buffer_size, cursor, "=");
    cursor = append_string(out_buffer, out_buffer_size, cursor, console_env_entries[i].value);
    cursor = append_string(out_buffer, out_buffer_size, cursor, "\n");
  }

  return cursor;
}

static void console_loaded_libs_reset(void) {
  size_t i = 0u;
  for (i = 0u; i < CONSOLE_LOADED_LIB_MAX; ++i) {
    console_loaded_libs[i].used = 0;
    console_loaded_libs[i].handle = 0u;
    console_loaded_libs[i].program_len = 0u;
    console_loaded_libs[i].ref_count = 0u;
    console_loaded_libs[i].owner_session_id = 0u;
    console_loaded_libs[i].owner_subject_id = 0u;
    console_loaded_libs[i].owner_actor[0] = '\0';
    console_loaded_libs[i].path[0] = '\0';
  }
  console_next_loaded_lib_handle = 1u;
}

static int console_loaded_lib_find_by_path(const char *path) {
  size_t i = 0u;

  if (path == 0 || path[0] == '\0') {
    return -1;
  }

  for (i = 0u; i < CONSOLE_LOADED_LIB_MAX; ++i) {
    if (console_loaded_libs[i].used && string_equals(console_loaded_libs[i].path, path)) {
      return (int)i;
    }
  }

  return -1;
}

static int console_loaded_lib_find_free_slot(void) {
  size_t i = 0u;

  for (i = 0u; i < CONSOLE_LOADED_LIB_MAX; ++i) {
    if (!console_loaded_libs[i].used) {
      return (int)i;
    }
  }

  return -1;
}

static int console_loaded_lib_find_by_handle(unsigned int handle) {
  size_t i = 0u;

  if (handle == 0u) {
    return -1;
  }

  for (i = 0u; i < CONSOLE_LOADED_LIB_MAX; ++i) {
    if (console_loaded_libs[i].used && console_loaded_libs[i].handle == handle) {
      return (int)i;
    }
  }

  return -1;
}

static int console_register_loaded_library(const char *resolved_path,
                                           size_t program_len,
                                           const char *owner_actor,
                                           unsigned int *out_handle) {
  int slot = -1;

  if (resolved_path == 0 || resolved_path[0] == '\0') {
    return 0;
  }

  slot = console_loaded_lib_find_by_path(resolved_path);
  if (slot >= 0) {
    if (out_handle != 0) {
      *out_handle = console_loaded_libs[slot].handle;
    }
    return 1;
  }

  slot = console_loaded_lib_find_free_slot();
  if (slot < 0) {
    return 0;
  }

  console_loaded_libs[slot].used = 1;
  console_loaded_libs[slot].handle = console_next_loaded_lib_handle++;
  console_loaded_libs[slot].program_len = program_len;
  console_loaded_libs[slot].ref_count = 0u;
  console_loaded_libs[slot].owner_session_id = session_manager_active_id();
  console_loaded_libs[slot].owner_subject_id = console_subject_id;
  if (owner_actor != 0 && owner_actor[0] != '\0') {
    copy_string(console_loaded_libs[slot].owner_actor,
                sizeof(console_loaded_libs[slot].owner_actor),
                owner_actor);
  } else {
    copy_string(console_loaded_libs[slot].owner_actor,
                sizeof(console_loaded_libs[slot].owner_actor),
                "unknown");
  }
  copy_string(console_loaded_libs[slot].path, sizeof(console_loaded_libs[slot].path), resolved_path);
  if (out_handle != 0) {
    *out_handle = console_loaded_libs[slot].handle;
  }

  return 1;
}

static int console_unregister_loaded_library(unsigned int handle, char *out_path, size_t out_path_size) {
  int slot = console_loaded_lib_find_by_handle(handle);

  if (out_path != 0 && out_path_size > 0u) {
    out_path[0] = '\0';
  }

  if (slot < 0) {
    return 0;
  }

  if (console_loaded_libs[slot].ref_count > 0u) {
    return 0;
  }

  if (out_path != 0 && out_path_size > 0u) {
    copy_string(out_path, out_path_size, console_loaded_libs[slot].path);
  }

  console_loaded_libs[slot].used = 0;
  console_loaded_libs[slot].handle = 0u;
  console_loaded_libs[slot].program_len = 0u;
  console_loaded_libs[slot].ref_count = 0u;
  console_loaded_libs[slot].owner_session_id = 0u;
  console_loaded_libs[slot].owner_subject_id = 0u;
  console_loaded_libs[slot].owner_actor[0] = '\0';
  console_loaded_libs[slot].path[0] = '\0';
  return 1;
}

static int console_get_loaded_library_ref_count(unsigned int handle, unsigned int *out_ref_count) {
  int slot = console_loaded_lib_find_by_handle(handle);

  if (out_ref_count != 0) {
    *out_ref_count = 0u;
  }

  if (slot < 0) {
    return 0;
  }

  if (out_ref_count != 0) {
    *out_ref_count = console_loaded_libs[slot].ref_count;
  }
  return 1;
}

static int console_acquire_loaded_library(unsigned int handle, unsigned int *out_ref_count) {
  int slot = console_loaded_lib_find_by_handle(handle);

  if (out_ref_count != 0) {
    *out_ref_count = 0u;
  }

  if (slot < 0) {
    return 0;
  }

  console_loaded_libs[slot].ref_count += 1u;
  if (out_ref_count != 0) {
    *out_ref_count = console_loaded_libs[slot].ref_count;
  }
  return 1;
}

static int console_release_loaded_library(unsigned int handle, unsigned int *out_ref_count) {
  int slot = console_loaded_lib_find_by_handle(handle);

  if (out_ref_count != 0) {
    *out_ref_count = 0u;
  }

  if (slot < 0 || console_loaded_libs[slot].ref_count == 0u) {
    return 0;
  }

  console_loaded_libs[slot].ref_count -= 1u;
  if (out_ref_count != 0) {
    *out_ref_count = console_loaded_libs[slot].ref_count;
  }
  return 1;
}

static size_t console_list_loaded_libraries(char *out_buffer, size_t out_buffer_size) {
  size_t i = 0u;
  size_t cursor = 0u;

  if (out_buffer == 0 || out_buffer_size == 0u) {
    return 0u;
  }

  out_buffer[0] = '\0';
  for (i = 0u; i < CONSOLE_LOADED_LIB_MAX; ++i) {
    if (!console_loaded_libs[i].used) {
      continue;
    }

    cursor = append_string(out_buffer, out_buffer_size, cursor, "handle=");
    cursor = append_u32_decimal(out_buffer, out_buffer_size, cursor, console_loaded_libs[i].handle);
    cursor = append_string(out_buffer, out_buffer_size, cursor, " path=");
    cursor = append_string(out_buffer, out_buffer_size, cursor, console_loaded_libs[i].path);
    cursor = append_string(out_buffer, out_buffer_size, cursor, " size=");
    cursor = append_u32_decimal(out_buffer,
                                out_buffer_size,
                                cursor,
                                (unsigned int)console_loaded_libs[i].program_len);
    cursor = append_string(out_buffer, out_buffer_size, cursor, " refs=");
    cursor = append_u32_decimal(out_buffer,
                                out_buffer_size,
                                cursor,
                                (unsigned int)console_loaded_libs[i].ref_count);
    cursor = append_string(out_buffer, out_buffer_size, cursor, " owner_session=");
    cursor = append_u32_decimal(out_buffer,
                                out_buffer_size,
                                cursor,
                                (unsigned int)console_loaded_libs[i].owner_session_id);
    cursor = append_string(out_buffer, out_buffer_size, cursor, " owner_subject=");
    cursor = append_u32_decimal(out_buffer,
                                out_buffer_size,
                                cursor,
                                (unsigned int)console_loaded_libs[i].owner_subject_id);
    cursor = append_string(out_buffer, out_buffer_size, cursor, " owner_actor=");
    cursor = append_string(out_buffer,
                           out_buffer_size,
                           cursor,
                           console_loaded_libs[i].owner_actor);
    cursor = append_string(out_buffer, out_buffer_size, cursor, "\n");
  }

  return cursor;
}

static void console_auth_cache_reset(void) {
  size_t i = 0u;
  for (i = 0u; i < CONSOLE_AUTH_CACHE_MAX; ++i) {
    console_auth_cache[i].used = 0;
    console_auth_cache[i].key[0] = '\0';
    console_auth_cache[i].decision = 0;
  }
}

static void console_auth_cache_build_key(char *out_key, size_t out_key_size,
                                         const char *category,
                                         const char *operation,
                                         const char *path) {
  size_t cursor = 0u;

  if (out_key == 0 || out_key_size == 0u) {
    return;
  }

  out_key[0] = '\0';
  cursor = append_string(out_key, out_key_size, cursor, category);
  cursor = append_string(out_key, out_key_size, cursor, ":");
  if (operation != 0 && operation[0] != '\0') {
    cursor = append_string(out_key, out_key_size, cursor, operation);
    cursor = append_string(out_key, out_key_size, cursor, ":");
  }
  (void)append_string(out_key, out_key_size, cursor, path != 0 ? path : "");
}

static int console_auth_cache_lookup(const char *key, auth_cache_decision_t *out_decision) {
  size_t i = 0u;

  if (key == 0 || key[0] == '\0' || out_decision == 0) {
    return 0;
  }

  for (i = 0u; i < CONSOLE_AUTH_CACHE_MAX; ++i) {
    if (console_auth_cache[i].used && string_equals(console_auth_cache[i].key, key)) {
      *out_decision = console_auth_cache[i].decision;
      return 1;
    }
  }
  return 0;
}

static int console_auth_cache_store(const char *key, auth_cache_decision_t decision) {
  size_t i = 0u;

  if (key == 0 || key[0] == '\0') {
    return 0;
  }

  for (i = 0u; i < CONSOLE_AUTH_CACHE_MAX; ++i) {
    if (console_auth_cache[i].used && string_equals(console_auth_cache[i].key, key)) {
      console_auth_cache[i].decision = decision;
      return 1;
    }
  }

  for (i = 0u; i < CONSOLE_AUTH_CACHE_MAX; ++i) {
    if (!console_auth_cache[i].used) {
      console_auth_cache[i].used = 1;
      copy_string(console_auth_cache[i].key, sizeof(console_auth_cache[i].key), key);
      console_auth_cache[i].decision = decision;
      return 1;
    }
  }

  return 0;
}

static size_t console_auth_cache_list(char *out_buffer, size_t out_buffer_size) {
  size_t i = 0u;
  size_t cursor = 0u;

  if (out_buffer == 0 || out_buffer_size == 0u) {
    return 0u;
  }

  out_buffer[0] = '\0';
  for (i = 0u; i < CONSOLE_AUTH_CACHE_MAX; ++i) {
    if (!console_auth_cache[i].used) {
      continue;
    }

    cursor = append_string(out_buffer, out_buffer_size, cursor, "  ");
    cursor = append_string(out_buffer, out_buffer_size, cursor, console_auth_cache[i].key);
    cursor = append_string(out_buffer, out_buffer_size, cursor, " -> ");
    cursor = append_string(out_buffer, out_buffer_size, cursor,
                           console_auth_cache[i].decision == AUTH_CACHE_ALLOW ? "allow" : "deny");
    cursor = append_string(out_buffer, out_buffer_size, cursor, "\n");
  }

  return cursor;
}

static void copy_path_component(const char *src, char *dst, size_t dst_size) {
  size_t i = 0u;

  if (dst_size == 0u) {
    return;
  }

  while (src[i] != '\0' && src[i] != '/' && src[i] != '\\' && i + 1u < dst_size) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

static void normalize_absolute_path(const char *input, char *out, size_t out_size) {
  const char *cursor = input;
  char component[32];
  size_t cursor_out = 0u;

  if (out_size == 0u) {
    return;
  }

  out[0] = '/';
  out[1] = '\0';

  if (input == 0 || input[0] == '\0') {
    return;
  }

  while (*cursor == '/' || *cursor == '\\') {
    ++cursor;
  }

  while (*cursor != '\0') {
    while (*cursor == '/' || *cursor == '\\') {
      ++cursor;
    }
    if (*cursor == '\0') {
      break;
    }

    copy_path_component(cursor, component, sizeof(component));

    if (string_equals(component, ".")) {
      while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
        ++cursor;
      }
      continue;
    }

    if (string_equals(component, "..")) {
      size_t len = string_len(out);
      if (len > 1u) {
        while (len > 1u && out[len - 1u] != '/') {
          out[len - 1u] = '\0';
          --len;
        }
        if (len > 1u) {
          out[len - 1u] = '\0';
        }
      }
      while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
        ++cursor;
      }
      continue;
    }

    cursor_out = string_len(out);
    if (cursor_out + 1u < out_size && out[cursor_out - 1u] != '/') {
      out[cursor_out++] = '/';
      out[cursor_out] = '\0';
    }
    append_string(out, out_size, cursor_out, component);

    while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
      ++cursor;
    }
  }
}

static void console_get_effective_cwd(char *out, size_t out_size) {
  char env_pwd[64];

  if (out == 0 || out_size == 0u) {
    return;
  }

  if (console_env_get("PWD", env_pwd, sizeof(env_pwd)) && console_directory_exists(env_pwd)) {
    copy_string(console_cwd, sizeof(console_cwd), env_pwd);
    copy_string(out, out_size, env_pwd);
    return;
  }

  if (console_directory_exists(console_cwd)) {
    (void)console_env_set("PWD", console_cwd);
    copy_string(out, out_size, console_cwd);
    return;
  }

  copy_string(console_cwd, sizeof(console_cwd), "/");
  (void)console_env_set("PWD", "/");
  copy_string(out, out_size, "/");
}

static void resolve_path(const char *path, char *out, size_t out_size) {
  char raw[128];
  char base_cwd[64];
  size_t cursor = 0u;

  if (out_size == 0u) {
    return;
  }

  console_get_effective_cwd(base_cwd, sizeof(base_cwd));

  if (path == 0 || path[0] == '\0') {
    copy_string(out, out_size, base_cwd);
    return;
  }

  raw[0] = '\0';

  if (path[0] == '/' || path[0] == '\\') {
    copy_string(raw, sizeof(raw), path);
    normalize_absolute_path(raw, out, out_size);
    return;
  }

  cursor = append_string(raw, sizeof(raw), cursor, base_cwd);
  if (cursor + 1u < sizeof(raw) && raw[cursor - 1u] != '/') {
    raw[cursor++] = '/';
    raw[cursor] = '\0';
  }
  append_string(raw, sizeof(raw), cursor, path);
  normalize_absolute_path(raw, out, out_size);
}

static int console_directory_exists(const char *absolute_path) {
  char probe[4];
  size_t probe_len = 0u;
  fs_result_t result = FS_ERR_INVALID_ARG;

  if (absolute_path == 0 || absolute_path[0] == '\0') {
    return 0;
  }

  result = fs_list_dir(absolute_path, probe, sizeof(probe), &probe_len);
  return result == FS_OK || result == FS_ERR_NO_SPACE;
}

static int console_change_directory(const char *absolute_path) {
  if (!console_directory_exists(absolute_path)) {
    return 0;
  }

  copy_string(console_cwd, sizeof(console_cwd), absolute_path);
  (void)console_env_set("PWD", absolute_path);
  return 1;
}

static int string_has_suffix(const char *value, const char *suffix) {
  size_t value_len = string_len(value);
  size_t suffix_len = string_len(suffix);
  size_t start = 0u;

  if (suffix_len > value_len) {
    return 0;
  }

  start = value_len - suffix_len;
  while (*suffix != '\0') {
    if (value[start++] != *suffix++) {
      return 0;
    }
  }
  return 1;
}

static const char *console_skip_path_delimiters(const char *value) {
  const char *cursor = value;

  if (cursor == 0) {
    return "";
  }

  while (*cursor == ' ' || *cursor == '\t' || *cursor == ';' || *cursor == ':') {
    ++cursor;
  }

  return cursor;
}

static const char *console_next_path_entry(const char *cursor,
                                           char *out_entry,
                                           size_t out_entry_size) {
  size_t write = 0u;

  if (out_entry == 0 || out_entry_size == 0u) {
    return 0;
  }

  out_entry[0] = '\0';
  if (cursor == 0) {
    return 0;
  }

  cursor = console_skip_path_delimiters(cursor);
  if (*cursor == '\0') {
    return 0;
  }

  while (cursor[write] != '\0' && cursor[write] != ';' && cursor[write] != ':') {
    if (write + 1u < out_entry_size) {
      out_entry[write] = cursor[write];
    }
    ++write;
  }

  if (out_entry_size > 0u) {
    size_t cap = out_entry_size - 1u;
    size_t len = write < cap ? write : cap;

    while (len > 0u && (out_entry[len - 1u] == ' ' || out_entry[len - 1u] == '\t')) {
      --len;
    }
    out_entry[len] = '\0';
  }

  cursor += write;
  while (*cursor == ';' || *cursor == ':') {
    ++cursor;
  }
  return cursor;
}

static void console_append_elf_suffix(char *path, size_t path_size) {
  size_t len = 0u;

  if (path == 0 || path_size == 0u) {
    return;
  }

  if (string_has_suffix(path, ".bin")) {
    return;
  }

  len = string_len(path);
  (void)append_string(path, path_size, len, ".bin");
}

static void console_build_app_candidate_from_dir(const char *base_dir,
                                                 const char *app_name,
                                                 char *out_path,
                                                 size_t out_path_size) {
  size_t cursor = 0u;

  if (out_path == 0 || out_path_size == 0u) {
    return;
  }

  out_path[0] = '\0';

  if (app_name == 0 || app_name[0] == '\0') {
    return;
  }

  cursor = append_string(out_path, out_path_size, cursor, base_dir);
  if (cursor > 0u && out_path[cursor - 1u] != '/' && out_path[cursor - 1u] != '\\') {
    if (cursor + 1u < out_path_size) {
      out_path[cursor++] = '/';
      out_path[cursor] = '\0';
    }
  }
  cursor = append_string(out_path, out_path_size, cursor, app_name);

  (void)cursor;
  console_append_elf_suffix(out_path, out_path_size);
}

static process_result_t console_try_run_candidate(const char *candidate_path,
                                                const char *app_args,
                                                const process_context_t *context,
                                                process_result_t *out_result) {
  process_result_t result = PROCESS_ERR_NOT_FOUND;

  if (candidate_path == 0 || candidate_path[0] == '\0' || context == 0) {
    if (out_result != 0) {
      *out_result = PROCESS_ERR_INVALID_ARG;
    }
    return PROCESS_ERR_INVALID_ARG;
  }

  result = process_run(candidate_path, app_args, context);
  if (out_result != 0) {
    *out_result = result;
  }

  return result;
}

static void console_write(const char *message) {
  size_t i = 0u;

  if (cap_table_check(console_subject_id, CAP_CONSOLE_WRITE) != CAP_OK) {
    return;
  }

  if (message == 0) {
    return;
  }

  if (!g_console_restoring_history && message != 0) {
    for (i = 0u; message[i] != '\0'; ++i) {
      if (console_screen_history_len + 1u >= CONSOLE_SCREEN_HISTORY_MAX) {
        break;
      }
      console_screen_history[console_screen_history_len++] = message[i];
    }
    console_screen_history[console_screen_history_len] = '\0';
  }

  serial_hal_write(message);
  video_hal_write(message);
}

static void console_clear_hardware(void) {
  video_hal_clear();
}

static void console_restore_screen_history(void) {
  console_clear_hardware();

  if (console_screen_history_len == 0u || console_screen_history[0] == '\0') {
    return;
  }

  g_console_restoring_history = 1;
  console_write(console_screen_history);
  g_console_restoring_history = 0;
}

static void console_write_prompt(void) {
  char prompt[32];
  size_t cursor = 0u;

  cursor = append_string(prompt, sizeof(prompt), cursor, "\nsecureos[s");
  cursor = append_u32_decimal(prompt, sizeof(prompt), cursor, session_manager_active_id());
  cursor = append_string(prompt, sizeof(prompt), cursor, "]> ");
  console_write(prompt);
}

static void console_emit_char(char value) {
  char out[2];
  out[0] = value;
  out[1] = '\0';
  console_write(out);
}

static void console_reset_line(void) {
  console_line_len = 0u;
  console_line[0] = '\0';
}

static size_t console_history_slot_to_index(size_t slot_from_newest) {
  size_t newest = (console_history_next + CONSOLE_HISTORY_MAX - 1u) % CONSOLE_HISTORY_MAX;
  return (newest + CONSOLE_HISTORY_MAX - slot_from_newest) % CONSOLE_HISTORY_MAX;
}

static void console_history_push_current_line(void) {
  size_t i = 0u;
  size_t newest_index = 0u;

  if (console_line_len == 0u) {
    return;
  }

  if (console_history_count > 0u) {
    newest_index = console_history_slot_to_index(0u);
    if (string_equals(console_history[newest_index], console_line)) {
      return;
    }
  }

  for (i = 0u; i + 1u < CONSOLE_LINE_MAX && console_line[i] != '\0'; ++i) {
    console_history[console_history_next][i] = console_line[i];
  }
  console_history[console_history_next][i] = '\0';

  console_history_next = (console_history_next + 1u) % CONSOLE_HISTORY_MAX;
  if (console_history_count < CONSOLE_HISTORY_MAX) {
    ++console_history_count;
  }
}

static void console_replace_input_line(const char *value) {
  size_t old_len = console_line_len;
  size_t i = 0u;

  while (value[i] != '\0' && i + 1u < CONSOLE_LINE_MAX) {
    console_line[i] = value[i];
    ++i;
  }
  console_line[i] = '\0';
  console_line_len = i;

  console_write("\r");
  console_write_prompt();
  console_write(console_line);

  while (i < old_len) {
    console_write(" ");
    ++i;
  }

  while (old_len > console_line_len) {
    console_write("\b");
    --old_len;
  }
}

static void console_history_recall_up(void) {
  size_t index = 0u;

  if (console_history_count == 0u) {
    return;
  }

  if (console_history_browse_index < 0) {
    copy_string(console_pending_line, sizeof(console_pending_line), console_line);
    console_pending_line_len = console_line_len;
    console_history_browse_index = 0;
  } else if ((size_t)(console_history_browse_index + 1) < console_history_count) {
    ++console_history_browse_index;
  }

  index = console_history_slot_to_index((size_t)console_history_browse_index);
  console_replace_input_line(console_history[index]);
}

static void console_history_recall_down(void) {
  size_t index = 0u;

  if (console_history_count == 0u || console_history_browse_index < 0) {
    return;
  }

  if (console_history_browse_index == 0) {
    console_history_browse_index = -1;
    console_replace_input_line(console_pending_line);
    console_pending_line_len = console_line_len;
    return;
  }

  --console_history_browse_index;
  index = console_history_slot_to_index((size_t)console_history_browse_index);
  console_replace_input_line(console_history[index]);
}

static char console_wait_for_yes_no(void) {
  for (;;) {
    char input = '\0';
    if (!serial_hal_try_read_char(&input)) {
      console_idle_wait();
      continue;
    }

    if (input >= 'A' && input <= 'Z') {
      input = (char)(input - 'A' + 'a');
    }

    if (input == 'y' || input == 'n' || input == 'a') {
      console_emit_char(input);
      console_write("\n");
      return input;
    }
  }
}

static cap_access_state_t console_authorize_disk_io(const char *operation, const char *path) {
  uint8_t request_payload[EVENT_PAYLOAD_MAX];
  uint8_t decision_payload[EVENT_PAYLOAD_MAX];
  size_t request_len = 0u;
  uint64_t correlation_id = console_next_correlation_id++;
  uint64_t sequence_id = 0u;
  char answer = '\0';
  char cache_key[CONSOLE_AUTH_CACHE_KEY_MAX];
  auth_cache_decision_t cached_decision;

  if (cap_table_check(console_subject_id, CAP_DISK_IO_REQUEST) != CAP_OK) {
    return CAP_ACCESS_DENY;
  }

  console_auth_cache_build_key(cache_key, sizeof(cache_key), "disk_io", operation, path);

  if (console_auth_cache_lookup(cache_key, &cached_decision)) {
    console_write("\n[auth-session] Disk operation requested (cached)\n");
    console_write("[auth-session] operation=");
    console_write(operation);
    console_write(" path=");
    console_write(path);
    console_write("\n");
    if (cached_decision == AUTH_CACHE_ALLOW) {
      console_write("[auth-session] decision=allow (cached)\n");
      return CAP_ACCESS_ALLOW;
    }
    console_write("[auth-session] decision=deny (cached)\n");
    return CAP_ACCESS_DENY;
  }

  request_len = append_string((char *)request_payload, sizeof(request_payload), 0u, operation);
  request_len = append_string((char *)request_payload, sizeof(request_payload), request_len, ":");
  request_len = append_string((char *)request_payload, sizeof(request_payload), request_len, path);

  (void)event_publish(console_subject_id,
                      EVENT_TOPIC_DISK_IO_REQUEST,
                      console_subject_id,
                      correlation_id,
                      request_payload,
                      request_len,
                      &sequence_id);

  console_write("\n[auth-session] Disk operation requested\n");
  console_write("[auth-session] operation=");
  console_write(operation);
  console_write(" path=");
  console_write(path);
  console_write("\n");
  console_write("[auth-session] allow? (y/n/a=always): ");
  answer = console_wait_for_yes_no();

  if (answer == 'y' || answer == 'a') {
    if (answer == 'a') {
      (void)console_auth_cache_store(cache_key, AUTH_CACHE_ALLOW);
    }
    copy_string((char *)decision_payload, sizeof(decision_payload), "allow");
    (void)event_publish(console_subject_id,
                        EVENT_TOPIC_DISK_IO_DECISION,
                        console_subject_id,
                        correlation_id,
                        decision_payload,
                        string_len((const char *)decision_payload),
                        &sequence_id);
    console_write("[auth-session] decision=allow");
    if (answer == 'a') {
      console_write(" (cached)");
    }
    console_write("\n");
    return CAP_ACCESS_ALLOW;
  }

  copy_string((char *)decision_payload, sizeof(decision_payload), "deny");
  (void)event_publish(console_subject_id,
                      EVENT_TOPIC_DISK_IO_DECISION,
                      console_subject_id,
                      correlation_id,
                      decision_payload,
                      string_len((const char *)decision_payload),
                      &sequence_id);
  console_write("[auth-session] decision=deny\n");
  return CAP_ACCESS_DENY;
}

static cap_access_state_t console_authorize_unsigned_binary(const char *binary_path) {
  uint8_t request_payload[EVENT_PAYLOAD_MAX];
  uint8_t decision_payload[EVENT_PAYLOAD_MAX];
  size_t request_len = 0u;
  uint64_t correlation_id = console_next_correlation_id++;
  uint64_t sequence_id = 0u;
  char answer = '\0';
  char cache_key[CONSOLE_AUTH_CACHE_KEY_MAX];
  auth_cache_decision_t cached_decision;

  console_auth_cache_build_key(cache_key, sizeof(cache_key), "unsigned", 0,
                               binary_path != 0 ? binary_path : "(unknown)");

  if (console_auth_cache_lookup(cache_key, &cached_decision)) {
    console_write("\n[codesign] Unsigned binary check (cached)\n");
    console_write("[codesign] path=");
    console_write(binary_path != 0 ? binary_path : "(unknown)");
    console_write("\n");
    if (cached_decision == AUTH_CACHE_ALLOW) {
      console_write("[codesign] decision=allow (cached)\n");
      return CAP_ACCESS_ALLOW;
    }
    console_write("[codesign] decision=deny (cached)\n");
    return CAP_ACCESS_DENY;
  }

  request_len = append_string((char *)request_payload, sizeof(request_payload), 0u, "unsigned:");
  request_len = append_string((char *)request_payload, sizeof(request_payload), request_len,
                              binary_path != 0 ? binary_path : "(unknown)");

  (void)event_publish(console_subject_id,
                      EVENT_TOPIC_DISK_IO_REQUEST,
                      console_subject_id,
                      correlation_id,
                      request_payload,
                      request_len,
                      &sequence_id);

  console_write("\n[codesign] WARNING: unsigned binary detected\n");
  console_write("[codesign] path=");
  console_write(binary_path != 0 ? binary_path : "(unknown)");
  console_write("\n");
  console_write("[codesign] This binary has no code signature.\n");
  console_write("[codesign] Run anyway? (y/n/a=always): ");
  answer = console_wait_for_yes_no();

  if (answer == 'y' || answer == 'a') {
    if (answer == 'a') {
      (void)console_auth_cache_store(cache_key, AUTH_CACHE_ALLOW);
    }
    copy_string((char *)decision_payload, sizeof(decision_payload), "allow-unsigned");
    (void)event_publish(console_subject_id,
                        EVENT_TOPIC_DISK_IO_DECISION,
                        console_subject_id,
                        correlation_id,
                        decision_payload,
                        string_len((const char *)decision_payload),
                        &sequence_id);
    console_write("[codesign] decision=allow (user accepted risk)");
    if (answer == 'a') {
      console_write(" (cached)");
    }
    console_write("\n");
    return CAP_ACCESS_ALLOW;
  }

  copy_string((char *)decision_payload, sizeof(decision_payload), "deny-unsigned");
  (void)event_publish(console_subject_id,
                      EVENT_TOPIC_DISK_IO_DECISION,
                      console_subject_id,
                      correlation_id,
                      decision_payload,
                      string_len((const char *)decision_payload),
                      &sequence_id);
  console_write("[codesign] decision=deny\n");
  return CAP_ACCESS_DENY;
}

static void console_command_run(const char *app_name, const char *app_args) {
  process_context_t context;
  process_result_t result;
  char executable_path[128];
  char path_value[128];
  char path_entry[64];
  char base_cwd[64];
  const char *path_cursor = 0;

  if (app_name == 0 || app_name[0] == '\0') {
    console_write("usage: run <app>\n");
    console_write_prompt();
    return;
  }

  if (string_starts_with(app_name, "/lib/") || string_starts_with(app_name, "\\lib\\") ||
      string_starts_with(app_name, "lib/") || string_starts_with(app_name, "lib\\")) {
    console_write("libraries cannot be run directly\n");
    console_write_prompt();
    return;
  }

  context.subject_id = console_subject_id;
  context.actor_name = app_name;
  context.output = console_write;
  context.authorize_disk_io = console_authorize_disk_io;
  context.authorize_unsigned = console_authorize_unsigned_binary;
  context.resolve_path = resolve_path;
  context.change_directory = console_change_directory;
  context.get_env = console_env_get;
  context.set_env = console_env_set;
  context.list_env = console_env_list;
  context.register_loaded_library = console_register_loaded_library;
  context.unregister_loaded_library = console_unregister_loaded_library;
  context.get_loaded_library_ref_count = console_get_loaded_library_ref_count;
  context.acquire_loaded_library = console_acquire_loaded_library;
  context.release_loaded_library = console_release_loaded_library;
  context.list_loaded_libraries = console_list_loaded_libraries;

  result = process_run(app_name, app_args, &context);

  if (result == PROCESS_OK) {
    console_write_prompt();
    return;
  }

  if (result == PROCESS_ERR_NOT_FOUND) {
    console_write("command not found\n");
  } else if (result == PROCESS_ERR_CAPABILITY) {
    console_write("app denied: missing capability\n");
  } else if (result == PROCESS_ERR_DENIED) {
    console_write("app denied: authorization rejected\n");
  } else if (result == PROCESS_ERR_STORAGE) {
    console_write("app failed: storage error\n");
  } else if (result == PROCESS_ERR_FORMAT) {
    console_write("app failed: invalid elf\n");
  } else if (result == PROCESS_ERR_LIBRARY) {
    console_write("app failed: libraries are not user-invokable\n");
  } else if (result == PROCESS_ERR_IN_USE) {
    console_write("app failed: library in use\n");
  } else if (result == PROCESS_ERR_SIGNATURE) {
    console_write("app blocked: code signature validation failed\n");
  } else {
    console_write("app failed\n");
  }

  console_write_prompt();
}

static int console_try_run_os_command(const char *command, const char *arg1, const char *arg2) {
  char app_args[CONSOLE_LINE_MAX];
  size_t cursor = 0u;

  if (command == 0 || command[0] == '\0') {
    return 0;
  }

  app_args[0] = '\0';
  if (arg1 != 0 && arg1[0] != '\0') {
    cursor = append_string(app_args, sizeof(app_args), cursor, arg1);
    if (arg2 != 0 && arg2[0] != '\0') {
      cursor = append_string(app_args, sizeof(app_args), cursor, " ");
      (void)append_string(app_args, sizeof(app_args), cursor, arg2);
    }
  }

  console_command_run(command, app_args);
  return 1;
}

static void console_command_exit(const char *mode) {
  uint8_t exit_code = 0x10u;

  if (mode == 0 || mode[0] == '\0') {
    console_write("usage: exit <pass|fail>\n");
    console_write_prompt();
    return;
  }

  if (cap_table_check(console_subject_id, CAP_DEBUG_EXIT) != CAP_OK) {
    console_write("denied: missing CAP_DEBUG_EXIT\n");
    console_write_prompt();
    return;
  }

  if (string_equals(mode, "pass")) {
    exit_code = 0x10u;
  } else if (string_equals(mode, "fail")) {
    exit_code = 0x11u;
  } else {
    console_write("usage: exit <pass|fail>\n");
    console_write_prompt();
    return;
  }

  console_write("debug exit\n");
  debug_exit_qemu(exit_code);
}

static unsigned int console_parse_u32(const char *value, int *ok) {
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

static void console_command_session(const char *arg1, const char *arg2) {
  char listing[256];
  unsigned int session_id = 0u;
  int ok = 0;

  if (arg1 == 0 || arg1[0] == '\0' || string_equals(arg1, "list")) {
    if (session_manager_list(listing, sizeof(listing)) > 0u) {
      console_write(listing);
    }
    console_write_prompt();
    return;
  }

  if (string_equals(arg1, "new")) {
    if (!session_manager_create(console_subject_id, &session_id)) {
      console_write("session create failed\n");
      console_write_prompt();
      return;
    }

    console_write("session created: ");
    listing[0] = '\0';
    (void)append_u32_decimal(listing, sizeof(listing), 0u, session_id);
    console_write(listing);
    console_write("\n");
    console_write_prompt();
    return;
  }

  if (string_equals(arg1, "switch")) {
    session_id = console_parse_u32(arg2, &ok);
    if (!ok || !session_manager_switch(session_id)) {
      console_write("session switch failed\n");
      console_write_prompt();
      return;
    }

    g_console_ctx->escape_state = 0u;
    console_reset_line();
    console_history_browse_index = -1;
    console_pending_line[0] = '\0';
    console_pending_line_len = 0u;
    console_restore_screen_history();
    console_write("switched to session ");
    listing[0] = '\0';
    (void)append_u32_decimal(listing, sizeof(listing), 0u, session_id);
    console_write(listing);
    console_write("\n");
    console_write_prompt();
    return;
  }

  console_write("usage: session [list|new|switch <id>]\n");
  console_write_prompt();
}

static void console_handle_session_hotkey(char key_code) {
  unsigned int target_session = 0u;
  char value[16];

  if (key_code < 'P' || key_code > 'S') {
    return;
  }

  target_session = (unsigned int)(key_code - 'P');
  if (!session_manager_switch(target_session)) {
    console_write("session switch failed\n");
    console_write_prompt();
    return;
  }

  g_console_ctx->escape_state = 0u;
  console_reset_line();
  console_history_browse_index = -1;
  console_pending_line[0] = '\0';
  console_pending_line_len = 0u;
  console_restore_screen_history();
  value[0] = '\0';
  (void)append_u32_decimal(value, sizeof(value), 0u, target_session);
  console_write("switched to session ");
  console_write(value);
  console_write("\n");
  console_write_prompt();
}

static void console_handle_command(void) {
  char command[16];
  char arg1[64];
  char arg2[CONSOLE_LINE_MAX];
  const char *cursor = skip_spaces(console_line);

  if (console_line_len == 0u) {
    console_write_prompt();
    return;
  }

  copy_until_space(command, sizeof(command), cursor, &cursor);
  cursor = skip_spaces(cursor);
  copy_until_space(arg1, sizeof(arg1), cursor, &cursor);
  cursor = skip_spaces(cursor);
  copy_string(arg2, sizeof(arg2), cursor);

  if (string_equals(command, "run")) {
    console_command_run(arg1, arg2);
    return;
  }

  if (string_equals(command, "clear")) {
    console_screen_history_len = 0u;
    console_screen_history[0] = '\0';
    console_clear_hardware();
    console_write_prompt();
    return;
  }

  if (string_equals(command, "exit")) {
    console_command_exit(arg1);
    return;
  }

  if (string_equals(command, "session")) {
    console_command_session(arg1, arg2);
    return;
  }

  if (string_equals(command, "auth-cache")) {
    if (string_equals(arg1, "reset") || string_equals(arg1, "clear")) {
      console_auth_cache_reset();
      console_write("auth cache cleared\n");
      console_write_prompt();
      return;
    }
    if (arg1[0] == '\0' || string_equals(arg1, "list")) {
      char listing[512];
      size_t len = console_auth_cache_list(listing, sizeof(listing));
      if (len == 0u) {
        console_write("auth cache is empty\n");
      } else {
        console_write("cached authorizations:\n");
        console_write(listing);
      }
      console_write_prompt();
      return;
    }
    console_write("usage: auth-cache [list|reset]\n");
    console_write_prompt();
    return;
  }

  (void)console_try_run_os_command(command, arg1, arg2);
}

void console_init(console_context_t *context, cap_subject_id_t subject_id) {
  if (context == 0) {
    return;
  }

  g_console_ctx = context;
  console_subject_id = subject_id;
  console_reset_line();
  console_pending_line_len = 0u;
  console_pending_line[0] = '\0';
  console_history_browse_index = -1;
  console_next_correlation_id = 1u;
  copy_string(console_cwd, sizeof(console_cwd), "/");
  g_console_ctx->escape_state = 0u;
  console_env_reset();
  console_loaded_libs_reset();
  console_auth_cache_reset();
  console_history_count = 0u;
  console_history_next = 0u;
  console_screen_history_len = 0u;
  console_screen_history[0] = '\0';
  (void)console_env_set("PWD", "/");
  (void)console_env_set("PATH", "/apps");

  console_write("TEST:START:console\n");
  console_write("SecureOS console ready\n");
  console_write("Type 'help' for commands\n");
  console_write_prompt();
  console_write("TEST:PASS:console\n");
}

void console_bind_context(console_context_t *context) {
  if (context != 0) {
    g_console_ctx = context;
    g_console_ctx->escape_state = 0u;
    console_history_browse_index = -1;
    console_pending_line[0] = '\0';
    console_pending_line_len = 0u;
  }
}

void console_run(void) {
  if (g_console_ctx == 0) {
    return;
  }

  for (;;) {
    char input = '\0';

    if (!serial_hal_try_read_char(&input)) {
      console_idle_wait();
      continue;
    }

    if (g_console_ctx->escape_state == 0u && input == 0x1Bu) {
      g_console_ctx->escape_state = 1u;
      continue;
    }

    if (g_console_ctx->escape_state == 1u) {
      if (input == 'O') {
        g_console_ctx->escape_state = 2u;
      } else if (input == '[') {
        g_console_ctx->escape_state = 3u;
      } else {
        g_console_ctx->escape_state = 0u;
      }
      continue;
    }

    if (g_console_ctx->escape_state == 2u) {
      g_console_ctx->escape_state = 0u;
      console_handle_session_hotkey(input);
      continue;
    }

    if (g_console_ctx->escape_state == 3u) {
      g_console_ctx->escape_state = 0u;
      if (input == 'A') {
        console_history_recall_up();
      } else if (input == 'B') {
        console_history_recall_down();
      }
      continue;
    }

    if (input == '\r' || input == '\n') {
      console_write("\n");
      console_line[console_line_len] = '\0';
      console_history_push_current_line();
      console_history_browse_index = -1;
      console_pending_line[0] = '\0';
      console_pending_line_len = 0u;
      console_handle_command();
      console_reset_line();
      continue;
    }

    if (input == 0x08 || input == 0x7F) {
      if (console_line_len > 0u) {
        --console_line_len;
        console_line[console_line_len] = '\0';
        console_write("\b \b");
      }
      continue;
    }

    if (console_line_len + 1u >= CONSOLE_LINE_MAX) {
      console_write("\nline too long\n");
      console_reset_line();
      console_write_prompt();
      continue;
    }

    console_line[console_line_len] = input;
    ++console_line_len;
    console_line[console_line_len] = '\0';
    console_emit_char(input);
  }
}
