/**
 * @file user/apps/edit/main.c
 * @brief Minimal line-oriented source editor for the guest console (issue #769).
 *
 * Purpose:
 *   Provides the smallest practical multiline source-creation/editing
 *   workflow inside SecureOS so a user can create, inspect, modify, save,
 *   and cancel a small C program entirely from the ordinary guest console
 *   (DEMO-05). It is deliberately NOT an IDE: one file per session,
 *   line-oriented commands, explicit size limits, no screen redraw.
 *
 * Workflow:
 *   Launched as `run /apps/edit`. The app reads characters through
 *   os_input_read_char (CAP_INPUT_KEYBOARD, granted to the console subject
 *   that console-launched apps inherit) and executes line commands.
 *   Commands:
 *     ?            print help
 *     o <name>     open-or-create a file (disk read, consent-gated)
 *     p            print the buffer as EDIT:BUF:<n>:<line> markers
 *     a            append lines until a line containing only "."
 *     c <n>        replace line n with the next input line
 *     d <n>        delete line n
 *     w            write the buffer to the open file (disk write, consent)
 *     q            quit; a dirty buffer is discarded, nothing is written
 *   Every observable result is echoed as a deterministic EDIT:* marker so
 *   the run_qemu kernel_edit gate can assert byte-exact content fidelity
 *   (spaces, quotes, backslashes and newlines survive untouched).
 *
 * Interactions:
 *   - secureos_api.h: os_console_write, os_input_read_char,
 *     os_fs_read_file_bytes, os_fs_write_file_bytes, os_process_exit.
 *   - kernel/core/console.c: disk-IO consent prompts ([auth-session]) are
 *     emitted by the kernel during the fs calls; the editor never bypasses
 *     them. A denied save leaves persistent content unchanged, and a quit
 *     with a dirty buffer performs no write at all.
 *   - build/scripts/run_qemu.sh: the kernel_edit scripted gate drives this
 *     app end-to-end on the ordinary booted image.
 *
 * Launched by:
 *   The guest console `run /apps/edit` command (staged + signed app, so no
 *   codesign prompt; one disk-IO prompt per executed file operation).
 */

#include "secureos_api.h"

enum {
  EDIT_LINE_MAX = 64u,      /* max bytes per logical line (excl. newline) */
  EDIT_LINES_MAX = 32u,     /* max lines held in the buffer */
  EDIT_CMD_MAX = 64u,       /* command/read-line input cap; overlong -> ERR */
  EDIT_FILE_MAX = 2048u     /* staging buffer for disk file contents */
};

/* All state is static (.bss): the editor workload is bounded by the
 * limits above, so no heap/arena is required (mirrors user/apps/binfs). */
static char g_lines[EDIT_LINES_MAX][EDIT_LINE_MAX];
static unsigned int g_line_count = 0u;
static char g_file_name[16];
static int g_file_open = 0;
static int g_dirty = 0;
static char g_file_buf[EDIT_FILE_MAX];
static unsigned int g_failures = 0u;

static void edit_log(const char *message) {
  (void)os_console_write(message);
}

static void edit_fail(const char *message) {
  edit_log(message);
  g_failures++;
}

static int edit_str_len(const char *s) {
  int n = 0;
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

static int edit_str_eq(const char *a, const char *b) {
  int i = 0;
  while (a[i] != '\0' && b[i] != '\0') {
    if (a[i] != b[i]) {
      return 0;
    }
    ++i;
  }
  return a[i] == b[i];
}

static void edit_str_copy(char *dst, const char *src, unsigned int cap) {
  unsigned int i = 0u;
  while (i + 1u < cap && src[i] != '\0') {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

/* Append a decimal unsigned value to out at *w, bounded by cap. */
static void edit_append_u(char *out, unsigned int *w, unsigned int cap,
                          unsigned int value) {
  char digits[10];
  int d = 0;
  if (value == 0u) {
    digits[d++] = '0';
  }
  while (value > 0u && d < (int)sizeof(digits)) {
    digits[d++] = (char)('0' + (int)(value % 10u));
    value /= 10u;
  }
  while (d > 0 && *w + 1u < cap) {
    out[(*w)++] = digits[--d];
  }
  out[*w] = '\0';
}

/* Emit "<label>" then the decimal value, e.g. EDIT:APPEND:4 */
static void edit_log_count(const char *label, unsigned int value) {
  char marker[32];
  unsigned int w = 0u;
  const char *p = label;
  while (*p != '\0' && w + 1u < sizeof(marker)) {
    marker[w++] = *p++;
  }
  marker[w] = '\0';
  edit_append_u(marker, &w, sizeof(marker), value);
  edit_log(marker);
  edit_log("\n");
}

static int edit_parse_u(const char *s, unsigned int *out) {
  unsigned int v = 0u;
  int seen = 0;
  if (s == 0 || *s == '\0') {
    return 0;
  }
  while (*s != '\0') {
    if (*s < '0' || *s > '9') {
      return 0;
    }
    if (v > EDIT_LINES_MAX) {
      return 0; /* guard against overflow on absurd input */
    }
    v = v * 10u + (unsigned int)(*s - '0');
    seen = 1;
    ++s;
  }
  if (!seen || v == 0u || v > EDIT_LINES_MAX) {
    return 0;
  }
  *out = v;
  return 1;
}

/*
 * Read one input line from the console keyboard. Characters are taken
 * verbatim: spaces, quotes, backslashes and any printable byte are kept
 * (issue #769: whitespace/quote/backslash fidelity). '\r' or '\n' ends
 * the line; a stray paired terminator (CRLF translation) simply yields an
 * empty next line, which the command loop harmlessly ignores. A line of
 * EDIT_CMD_MAX-1 bytes or more is an explicit error — EDIT:ERR:LINE_LONG
 * with the remainder drained up to the newline (never silently truncated
 * into the buffer). Returns length, or -1 for the oversize case.
 */
static int edit_read_line(char *line, unsigned int cap) {
  unsigned int len = 0u;
  int overflow = 0;

  line[0] = '\0';
  for (;;) {
    char ch = '\0';
    os_status_t st = os_input_read_char(&ch);
    if (st != OS_STATUS_OK) {
      /* No data available (or input denied): the app owns the console
       * while it runs, so polling is also the idle wait. */
      continue;
    }
    if (ch == '\n' || ch == '\r') {
      break;
    }
    if (len + 1u < cap) {
      line[len] = ch;
      ++len;
      line[len] = '\0';
    } else {
      overflow = 1; /* keep draining until the line terminator */
    }
  }
  if (overflow) {
    edit_fail("EDIT:ERR:LINE_LONG\n");
    return -1;
  }
  return (int)len;
}

static void edit_help(void) {
  edit_log("EDIT:HELP:o <file> open|create, p print, a append(.), c <n> "
           "change, d <n> delete, w save, q quit, ? help\n");
}

static void edit_print_lines(void) {
  unsigned int i;
  char marker[32];
  for (i = 0u; i < g_line_count; ++i) {
    unsigned int w = 0u;
    const char *p = "EDIT:BUF:";
    while (*p != '\0' && w + 1u < sizeof(marker)) {
      marker[w++] = *p++;
    }
    marker[w] = '\0';
    edit_append_u(marker, &w, sizeof(marker), i + 1u);
    if (w + 2u < sizeof(marker)) {
      marker[w++] = ':';
      marker[w] = '\0';
    }
    edit_log(marker);
    edit_log(g_lines[i]);
    edit_log("\n");
  }
  edit_log_count("EDIT:PRINT:", g_line_count);
}

/* Split g_file_buf[0..len) into the line buffer ('\n'-separated). Lines
 * longer than EDIT_LINE_MAX-1 bytes or files with more than
 * EDIT_LINES_MAX lines are a hard fit error (explicit, no truncation). */
static int edit_load_buffer(unsigned int len) {
  unsigned int start = 0u;
  unsigned int i;
  g_line_count = 0u;
  for (i = 0u; i <= len; ++i) {
    if (i == len || g_file_buf[i] == '\n') {
      unsigned int line_len = i - start;
      if (i == len && line_len == 0u) {
        break; /* trailing newline: no phantom empty last line */
      }
      if (line_len + 1u > EDIT_LINE_MAX) {
        g_file_open = 0;
        edit_fail("EDIT:ERR:LINE_FIT\n");
        return 0;
      }
      if (g_line_count >= EDIT_LINES_MAX) {
        g_file_open = 0;
        edit_fail("EDIT:ERR:LINE_COUNT_FIT\n");
        return 0;
      }
      edit_str_copy(g_lines[g_line_count], &g_file_buf[start], line_len + 1u);
      ++g_line_count;
      start = i + 1u;
    }
  }
  return 1;
}

static int edit_join_buffer(unsigned int *out_len) {
  unsigned int pos = 0u;
  unsigned int i;
  for (i = 0u; i < g_line_count; ++i) {
    int l = edit_str_len(g_lines[i]);
    int j;
    if (pos + (unsigned int)l + 1u > (unsigned int)sizeof(g_file_buf)) {
      return 0;
    }
    for (j = 0; j < l; ++j) {
      g_file_buf[pos++] = g_lines[i][j];
    }
    g_file_buf[pos++] = '\n';
  }
  *out_len = pos;
  return 1;
}

static void edit_cmd_open(const char *name) {
  os_status_t st;
  unsigned int len = 0u;
  if (name[0] == '\0') {
    edit_fail("EDIT:ERR:NO_NAME\n");
    return;
  }
  edit_str_copy(g_file_name, name, sizeof(g_file_name));
  st = os_fs_read_file_bytes(g_file_name, g_file_buf,
                             (unsigned int)sizeof(g_file_buf), &len);
  if (st == OS_STATUS_OK) {
    if (!edit_load_buffer(len)) {
      return;
    }
    g_file_open = 1;
    g_dirty = 0;
    edit_log("EDIT:OPEN:EXISTING\n");
    return;
  }
  if (st == OS_STATUS_NOT_FOUND && len == 0u) {
    /* New file: open with an empty buffer; nothing is written until 'w'. */
    g_line_count = 0u;
    g_file_open = 1;
    g_dirty = 0;
    edit_log("EDIT:OPEN:NEW\n");
    return;
  }
  if (st == OS_STATUS_DENIED) {
    edit_fail("EDIT:ERR:OPEN_DENIED\n");
    return;
  }
  edit_fail("EDIT:ERR:OPEN\n");
}

/* Append input lines until a line containing only "."; a "." alone is a
 * command terminator and is never added. An oversize append line reports
 * EDIT:ERR:LINE_LONG and ends append mode (explicit, no truncation). */
static void edit_cmd_append(void) {
  for (;;) {
    char line[EDIT_CMD_MAX];
    int r = edit_read_line(line, sizeof(line));
    if (r < 0) {
      return;
    }
    if (edit_str_eq(line, ".")) {
      edit_log_count("EDIT:APPEND:", g_line_count);
      return;
    }
    if (edit_str_len(line) + 1 > (int)EDIT_LINE_MAX) {
      edit_fail("EDIT:ERR:LINE_FIT\n");
      return;
    }
    if (g_line_count >= EDIT_LINES_MAX) {
      edit_fail("EDIT:ERR:LINE_COUNT_FIT\n");
      return;
    }
    edit_str_copy(g_lines[g_line_count], line, EDIT_LINE_MAX);
    ++g_line_count;
    g_dirty = 1;
  }
}

static void edit_cmd_change(const char *arg) {
  unsigned int n;
  char line[EDIT_CMD_MAX];
  int r;
  if (!edit_parse_u(arg, &n) || n > g_line_count) {
    edit_fail("EDIT:ERR:CHANGE_RANGE\n");
    return;
  }
  r = edit_read_line(line, sizeof(line));
  if (r < 0) {
    return;
  }
  if (edit_str_len(line) + 1 > (int)EDIT_LINE_MAX) {
    edit_fail("EDIT:ERR:LINE_FIT\n");
    return;
  }
  edit_str_copy(g_lines[n - 1u], line, EDIT_LINE_MAX);
  g_dirty = 1;
  edit_log("EDIT:CHANGE:");
  edit_log(g_lines[n - 1u]);
  edit_log("\n");
}

static void edit_cmd_delete(const char *arg) {
  unsigned int n;
  unsigned int i;
  if (!edit_parse_u(arg, &n) || n > g_line_count) {
    edit_fail("EDIT:ERR:DELETE_RANGE\n");
    return;
  }
  for (i = n - 1u; i + 1u < g_line_count; ++i) {
    edit_str_copy(g_lines[i], g_lines[i + 1u], EDIT_LINE_MAX);
  }
  --g_line_count;
  g_dirty = 1;
  edit_log_count("EDIT:DEL:", n);
}

static void edit_cmd_write(void) {
  os_status_t st;
  unsigned int len = 0u;
  if (!g_file_open) {
    edit_fail("EDIT:ERR:NO_FILE\n");
    return;
  }
  if (!edit_join_buffer(&len)) {
    edit_fail("EDIT:ERR:FILE_FIT\n");
    return;
  }
  st = os_fs_write_file_bytes(g_file_name, g_file_buf, len, 0);
  if (st == OS_STATUS_OK) {
    g_dirty = 0;
    edit_log("EDIT:SAVE:OK\n");
    return;
  }
  if (st == OS_STATUS_DENIED) {
    /* Consent denial: persistent content is untouched by contract. */
    edit_log("EDIT:SAVE:DENIED\n");
    return;
  }
  edit_fail("EDIT:ERR:SAVE\n");
}

static void edit_trim_token(char *tok) {
  int end = edit_str_len(tok);
  while (end > 0 && (tok[end - 1] == ' ' || tok[end - 1] == '\t')) {
    tok[end - 1] = '\0';
    --end;
  }
}

int main(void) {
  char line[EDIT_CMD_MAX];

  edit_log("EDIT:start\n");
  /* Readiness marker: the prompt loop is live and polling the keyboard;
   * the kernel_edit harness keys its first scripted keystrokes on it. */
  edit_log("EDIT:ready\n");

  for (;;) {
    int r;
    char *cmd;
    char *arg;

    edit_log("ed> ");
    r = edit_read_line(line, sizeof(line));
    if (r < 0) {
      continue; /* oversize line already reported; session stays alive */
    }

    cmd = line;
    while (*cmd == ' ') {
      ++cmd;
    }
    arg = cmd;
    while (*arg != '\0' && *arg != ' ') {
      ++arg;
    }
    if (*arg == ' ') {
      *arg = '\0';
      ++arg;
      while (*arg == ' ') {
        ++arg;
      }
    }
    edit_trim_token(arg);

    if (edit_str_eq(cmd, "?")) {
      edit_help();
    } else if (edit_str_eq(cmd, "o")) {
      edit_cmd_open(arg);
    } else if (edit_str_eq(cmd, "p")) {
      edit_print_lines();
    } else if (edit_str_eq(cmd, "a")) {
      if (!g_file_open) {
        edit_fail("EDIT:ERR:NO_FILE\n");
      } else {
        edit_cmd_append();
      }
    } else if (edit_str_eq(cmd, "c")) {
      edit_cmd_change(arg);
    } else if (edit_str_eq(cmd, "d")) {
      edit_cmd_delete(arg);
    } else if (edit_str_eq(cmd, "w")) {
      edit_cmd_write();
    } else if (edit_str_eq(cmd, "q")) {
      /* Cancel semantics: a dirty buffer is discarded with no write. */
      edit_log(g_dirty ? "EDIT:QUIT:DISCARD\n" : "EDIT:QUIT:CLEAN\n");
      edit_log("EDIT:done\n");
      break;
    } else if (cmd[0] == '\0') {
      /* empty line: repaint prompt */
    } else {
      edit_fail("EDIT:ERR:UNKNOWN_CMD\n");
    }
  }

  (void)os_process_exit(g_failures == 0u ? 0 : 1);
  /* The in-guest process-exit bridge does not return. Preserve an accurate
   * fallback status for host/static builds where no bridge is attached. */
  return g_failures == 0u ? 0 : 1;
}
