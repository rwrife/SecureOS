#include "console.h"

#include <stdint.h>

#include "../arch/x86/debug_exit.h"
#include "../arch/x86/serial.h"
#include "../arch/x86/vga.h"
#include "../cap/cap_table.h"
#include "../event/event_bus.h"
#include "../fs/fs_service.h"
#include "../hal/storage_hal.h"
#include "../user/app_runtime.h"

#define CONSOLE_LINE_MAX 128
#define CONSOLE_OUTPUT_MAX 512

static cap_subject_id_t console_subject_id;
static char console_line[CONSOLE_LINE_MAX];
static size_t console_line_len;
static uint64_t console_next_correlation_id;

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

static size_t append_u32_decimal(char *dst, size_t dst_size, size_t cursor, uint32_t value) {
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

static void console_command_ls(void) {
  char output[CONSOLE_OUTPUT_MAX];
  size_t len = 0u;

  if (cap_table_check(console_subject_id, CAP_FS_READ) != CAP_OK) {
    console_write("denied: missing CAP_FS_READ\n");
    console_write_prompt();
    return;
  }

  if (console_authorize_disk_io("ls", "/") != CAP_ACCESS_ALLOW) {
    console_write("disk access denied\n");
    console_write_prompt();
    return;
  }

  if (fs_list_root(output, sizeof(output), &len) != FS_OK) {
    console_write("ls failed\n");
    console_write_prompt();
    return;
  }

  if (len == 0u) {
    console_write("(empty)\n");
  } else {
    console_write(output);
  }
  console_write_prompt();
}

static void console_command_cat(const char *path) {
  char output[CONSOLE_OUTPUT_MAX];
  size_t len = 0u;

  if (path == 0 || path[0] == '\0') {
    console_write("usage: cat <file>\n");
    console_write_prompt();
    return;
  }

  if (cap_table_check(console_subject_id, CAP_FS_READ) != CAP_OK) {
    console_write("denied: missing CAP_FS_READ\n");
    console_write_prompt();
    return;
  }

  if (console_authorize_disk_io("cat", path) != CAP_ACCESS_ALLOW) {
    console_write("disk access denied\n");
    console_write_prompt();
    return;
  }

  if (fs_read_file(path, output, sizeof(output), &len) != FS_OK) {
    console_write("file not found\n");
    console_write_prompt();
    return;
  }

  console_write(output);
  console_write("\n");
  console_write_prompt();
}

static void console_command_write_like(const char *path, const char *content, int append) {
  if (path == 0 || path[0] == '\0' || content == 0 || content[0] == '\0') {
    if (append) {
      console_write("usage: append <file> <text>\n");
    } else {
      console_write("usage: write <file> <text>\n");
    }
    console_write_prompt();
    return;
  }

  if (cap_table_check(console_subject_id, CAP_FS_WRITE) != CAP_OK) {
    console_write("denied: missing CAP_FS_WRITE\n");
    console_write_prompt();
    return;
  }

  if (console_authorize_disk_io(append ? "append" : "write", path) != CAP_ACCESS_ALLOW) {
    console_write("disk access denied\n");
    console_write_prompt();
    return;
  }

  if (fs_write_file(path, content, append) != FS_OK) {
    console_write("write failed\n");
    console_write_prompt();
    return;
  }

  console_write("ok\n");
  console_write_prompt();
}

static void console_command_run(const char *app_name) {
  app_runtime_context_t context;
  app_runtime_result_t result;

  if (app_name == 0 || app_name[0] == '\0') {
    console_write("usage: run <app>\n");
    console_write_prompt();
    return;
  }

  context.subject_id = 1u;
  context.output = console_write;
  context.authorize_disk_io = console_authorize_disk_io;

  result = app_runtime_run(app_name, &context);
  if (result == APP_RUNTIME_OK) {
    console_write_prompt();
    return;
  }

  if (result == APP_RUNTIME_ERR_NOT_FOUND) {
    console_write("app not found\n");
  } else if (result == APP_RUNTIME_ERR_CAPABILITY) {
    console_write("app denied: missing capability\n");
  } else if (result == APP_RUNTIME_ERR_DENIED) {
    console_write("app denied: authorization rejected\n");
  } else if (result == APP_RUNTIME_ERR_STORAGE) {
    console_write("app failed: storage error\n");
  } else {
    console_write("app failed\n");
  }

  console_write_prompt();
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

static void console_command_apps(void) {
  char output[CONSOLE_OUTPUT_MAX];
  size_t len = 0u;

  len = app_runtime_list(output, sizeof(output));
  if (len == 0u) {
    console_write("(no apps)\n");
    console_write_prompt();
    return;
  }

  console_write(output);
  console_write_prompt();
}

static void console_command_storage(void) {
  char output[CONSOLE_OUTPUT_MAX];
  size_t cursor = 0u;

  cursor = append_string(output, sizeof(output), cursor, "storage backend=");
  cursor = append_string(output, sizeof(output), cursor, storage_hal_backend_name());
  cursor = append_string(output, sizeof(output), cursor, " blocks=");
  cursor = append_u32_decimal(output, sizeof(output), cursor, storage_hal_block_count());
  cursor = append_string(output, sizeof(output), cursor, " block_size=");
  cursor = append_u32_decimal(output, sizeof(output), cursor, storage_hal_block_size());
  cursor = append_string(output, sizeof(output), cursor, "\n");

  if (cursor > 0u) {
    console_write(output);
  }
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

  if (string_equals(command, "help")) {
    console_write("commands: help, ping, echo <text>, ls, cat <file>, write <file> <text>, append <file> <text>, storage, apps, run <app>, exit <pass|fail>\n");
    console_write_prompt();
    return;
  }

  if (string_equals(command, "ping")) {
    console_write("pong\n");
    console_write_prompt();
    return;
  }

  if (string_equals(command, "echo")) {
    console_write(arg1);
    if (arg2[0] != '\0') {
      console_write(" ");
      console_write(arg2);
    }
    console_write("\n");
    console_write_prompt();
    return;
  }

  if (string_equals(command, "ls")) {
    console_command_ls();
    return;
  }

  if (string_equals(command, "cat")) {
    console_command_cat(arg1);
    return;
  }

  if (string_equals(command, "write")) {
    console_command_write_like(arg1, arg2, 0);
    return;
  }

  if (string_equals(command, "append")) {
    console_command_write_like(arg1, arg2, 1);
    return;
  }

  if (string_equals(command, "apps")) {
    console_command_apps();
    return;
  }

  if (string_equals(command, "storage")) {
    console_command_storage();
    return;
  }

  if (string_equals(command, "run")) {
    console_command_run(arg1);
    return;
  }

  if (string_equals(command, "exit")) {
    console_command_exit(arg1);
    return;
  }

  console_write("unknown command\n");
  console_write_prompt();
}

void console_init(cap_subject_id_t subject_id) {
  console_subject_id = subject_id;
  console_reset_line();
  console_next_correlation_id = 1u;

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
