#include "console.h"

#include <stdint.h>

#include "../arch/x86/debug_exit.h"
#include "../arch/x86/serial.h"
#include "../arch/x86/vga.h"
#include "../cap/cap_table.h"
#include "../event/event_bus.h"
#include "../fs/fs_service.h"
#include "../user/app_runtime.h"

#define CONSOLE_LINE_MAX 128
#define CONSOLE_OUTPUT_MAX 512

static cap_subject_id_t console_subject_id;
static char console_line[CONSOLE_LINE_MAX];
static size_t console_line_len;
static uint64_t console_next_correlation_id;
static char console_cwd[64];

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

static void resolve_path(const char *path, char *out, size_t out_size) {
  char raw[64];
  size_t cursor = 0u;

  if (out_size == 0u) {
    return;
  }

  if (path == 0 || path[0] == '\0') {
    copy_string(out, out_size, console_cwd);
    return;
  }

  raw[0] = '\0';

  if (path[0] == '/' || path[0] == '\\') {
    copy_string(raw, sizeof(raw), path);
    normalize_absolute_path(raw, out, out_size);
    return;
  }

  cursor = append_string(raw, sizeof(raw), cursor, console_cwd);
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

  if (absolute_path == 0 || absolute_path[0] == '\0') {
    return 0;
  }

  return fs_list_dir(absolute_path, probe, sizeof(probe), &probe_len) == FS_OK;
}

static int console_change_directory(const char *absolute_path) {
  if (!console_directory_exists(absolute_path)) {
    return 0;
  }

  copy_string(console_cwd, sizeof(console_cwd), absolute_path);
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

static void console_resolve_app_path(const char *app_name, char *out_path, size_t out_path_size) {
  size_t cursor = 0u;

  if (out_path == 0 || out_path_size == 0u) {
    return;
  }

  out_path[0] = '\0';

  if (app_name == 0 || app_name[0] == '\0') {
    return;
  }

  if (app_name[0] == '/' || app_name[0] == '\\') {
    cursor = append_string(out_path, out_path_size, cursor, app_name);
  } else {
    cursor = append_string(out_path, out_path_size, cursor, "/os/");
    cursor = append_string(out_path, out_path_size, cursor, app_name);
  }

  if (!string_has_suffix(out_path, ".elf")) {
    (void)append_string(out_path, out_path_size, cursor, ".elf");
  }
}

static void console_write(const char *message) {
  if (cap_table_check(console_subject_id, CAP_CONSOLE_WRITE) != CAP_OK) {
    return;
  }

  serial_write(message);
  vga_write(message);
}

static void console_write_prompt(void) {
  console_write("secureos> ");
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

static char console_wait_for_yes_no(void) {
  for (;;) {
    char input = '\0';
    if (!serial_try_read_char(&input)) {
      console_idle_wait();
      continue;
    }

    if (input >= 'A' && input <= 'Z') {
      input = (char)(input - 'A' + 'a');
    }

    if (input == 'y' || input == 'n') {
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

  if (cap_table_check(console_subject_id, CAP_DISK_IO_REQUEST) != CAP_OK) {
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
  console_write("[auth-session] allow? (y/n): ");
  answer = console_wait_for_yes_no();

  if (answer == 'y') {
    copy_string((char *)decision_payload, sizeof(decision_payload), "allow");
    (void)event_publish(console_subject_id,
                        EVENT_TOPIC_DISK_IO_DECISION,
                        console_subject_id,
                        correlation_id,
                        decision_payload,
                        string_len((const char *)decision_payload),
                        &sequence_id);
    console_write("[auth-session] decision=allow\n");
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

static void console_command_run(const char *app_name, const char *app_args) {
  app_runtime_context_t context;
  app_runtime_result_t result;
  char executable_path[64];

  if (app_name == 0 || app_name[0] == '\0') {
    console_write("usage: run <app>\n");
    console_write_prompt();
    return;
  }

  console_resolve_app_path(app_name, executable_path, sizeof(executable_path));

  context.subject_id = 1u;
  context.output = console_write;
  context.authorize_disk_io = console_authorize_disk_io;
  context.resolve_path = resolve_path;
  context.change_directory = console_change_directory;

  result = app_runtime_run(executable_path, app_args, &context);
  if (result == APP_RUNTIME_OK) {
    console_write_prompt();
    return;
  }

  if (result == APP_RUNTIME_ERR_NOT_FOUND) {
    console_write("not found\n");
  } else if (result == APP_RUNTIME_ERR_CAPABILITY) {
    console_write("app denied: missing capability\n");
  } else if (result == APP_RUNTIME_ERR_DENIED) {
    console_write("app denied: authorization rejected\n");
  } else if (result == APP_RUNTIME_ERR_STORAGE) {
    console_write("app failed: storage error\n");
  } else if (result == APP_RUNTIME_ERR_FORMAT) {
    console_write("app failed: invalid elf\n");
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

  if (string_equals(command, "exit")) {
    console_command_exit(arg1);
    return;
  }

  (void)console_try_run_os_command(command, arg1, arg2);
}

void console_init(cap_subject_id_t subject_id) {
  console_subject_id = subject_id;
  console_reset_line();
  console_next_correlation_id = 1u;
  copy_string(console_cwd, sizeof(console_cwd), "/");

  console_write("TEST:START:console\n");
  console_write("SecureOS console ready\n");
  console_write("Type 'help' for commands\n");
  console_write_prompt();
  console_write("TEST:PASS:console\n");
}

void console_run(void) {
  for (;;) {
    char input = '\0';

    if (!serial_try_read_char(&input)) {
      console_idle_wait();
      continue;
    }

    if (input == '\r' || input == '\n') {
      console_write("\n");
      console_line[console_line_len] = '\0';
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
