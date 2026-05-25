/**
 * @file main.c
 * @brief sosh — SecureOS Shell script runner application.
 *
 * Purpose:
 *   User-space application that reads a script file from the filesystem
 *   and executes it using the sosh interpreter library. Acts as the CLI
 *   entry point for running .sosh scripts.
 *
 * Usage:
 *   sosh <script-path> [args...]
 *
 * Interactions:
 *   - secureos_api.h: uses OS syscalls for file I/O and console output.
 *   - soshlib (sosh.h): the interpreter engine.
 *   - launcher_exec.c: exec callback dispatches back to process_run.
 *
 * Launched by:
 *   Invoked as a user-space application via "sosh /scripts/demo.sosh"
 *   or via the console. Built as a standalone ELF and wrapped as SOF.
 */

#include "secureos_api.h"

/* --- Minimal string utilities (freestanding, no libc) --- */

static int sosh_app_strlen(const char *s) {
  int len = 0;
  if (s == 0) return 0;
  while (s[len]) len++;
  return len;
}

static void sosh_app_strcpy(char *dst, const char *src, int max) {
  int i = 0;
  if (src == 0) { dst[0] = '\0'; return; }
  while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

static void sosh_app_strcat(char *dst, const char *src, int max) {
  int dlen = sosh_app_strlen(dst);
  int i = 0;
  if (src == 0) return;
  while (src[i] && dlen + i < max - 1) { dst[dlen + i] = src[i]; i++; }
  dst[dlen + i] = '\0';
}

static int sosh_app_streq(const char *a, const char *b) {
  while (*a && *b) { if (*a != *b) return 0; a++; b++; }
  return *a == *b;
}

/* --- sosh interpreter (inline, since we link statically) --- */
/* Include the sosh library sources directly for the app build */
#include "../../libs/soshlib/sosh_lexer.h"
#include "../../libs/soshlib/sosh_vars.h"
#include "../../libs/soshlib/sosh_builtins.h"
#include "../../libs/soshlib/sosh_eval.h"

/* We need the implementations too — include .c files for static linking */
#include "../../libs/soshlib/sosh_lexer.c"
#include "../../libs/soshlib/sosh_vars.c"
#include "../../libs/soshlib/sosh_builtins.c"
#include "../../libs/soshlib/sosh_eval.c"

/* --- Callbacks --------------------------------------------------------- */

static void sosh_app_output(const char *text, void *ctx) {
  (void)ctx;
  if (text) os_console_write(text);
}

static int sosh_app_exec(const char *command, const char *args,
                         char *out_buf, int out_buf_size, void *ctx) {
  (void)ctx;

  if (sosh_app_streq(command, "__cat_raw")) {
    /* Special: read file content for 'source' */
    if (out_buf && out_buf_size > 0) {
      os_status_t st = os_fs_read_file(args, out_buf, (unsigned int)out_buf_size);
      return (st == OS_STATUS_OK) ? 0 : 1;
    }
    return 1;
  }

  if (sosh_app_streq(command, "exists")) {
    /* Check if path exists by trying to read it */
    char tmp[64];
    os_status_t st = os_fs_list_dir(args, tmp, sizeof(tmp));
    if (st == OS_STATUS_OK) return 0;
    st = os_fs_read_file(args, tmp, sizeof(tmp));
    return (st == OS_STATUS_OK) ? 0 : 1;
  }

  if (sosh_app_streq(command, "echo")) {
    /* echo is handled as a builtin, but if it arrives here... */
    if (out_buf && out_buf_size > 0) {
      sosh_app_strcpy(out_buf, args, out_buf_size);
      sosh_app_strcat(out_buf, "\n", out_buf_size);
    }
    return 0;
  }

  if (sosh_app_streq(command, "cat")) {
    if (out_buf && out_buf_size > 0) {
      os_status_t st = os_fs_read_file(args, out_buf, (unsigned int)out_buf_size);
      return (st == OS_STATUS_OK) ? 0 : 1;
    }
    return 1;
  }

  if (sosh_app_streq(command, "ls")) {
    if (out_buf && out_buf_size > 0) {
      const char *path = (args && args[0]) ? args : "/";
      os_status_t st = os_fs_list_dir(path, out_buf, (unsigned int)out_buf_size);
      return (st == OS_STATUS_OK) ? 0 : 1;
    }
    return 1;
  }

  if (sosh_app_streq(command, "write")) {
    /* write <path> <content> */
    char path[256];
    const char *content = args;
    int pi = 0;
    if (args == 0) return 1;
    while (*content && *content != ' ' && pi < 255) {
      path[pi++] = *content++;
    }
    path[pi] = '\0';
    while (*content == ' ') content++;
    os_status_t st = os_fs_write_file(path, content, 0);
    return (st == OS_STATUS_OK) ? 0 : 1;
  }

  if (sosh_app_streq(command, "append")) {
    char path[256];
    const char *content = args;
    int pi = 0;
    if (args == 0) return 1;
    while (*content && *content != ' ' && pi < 255) {
      path[pi++] = *content++;
    }
    path[pi] = '\0';
    while (*content == ' ') content++;
    os_status_t st = os_fs_write_file(path, content, 1);
    return (st == OS_STATUS_OK) ? 0 : 1;
  }

  if (sosh_app_streq(command, "mkdir")) {
    os_status_t st = os_fs_mkdir(args);
    return (st == OS_STATUS_OK) ? 0 : 1;
  }

  if (sosh_app_streq(command, "env")) {
    /* env KEY=VALUE sets env, env alone lists */
    if (args && args[0]) {
      /* Find '=' */
      char key[64];
      const char *val;
      int ki = 0;
      const char *p = args;
      while (*p && *p != '=' && ki < 63) { key[ki++] = *p++; }
      key[ki] = '\0';
      if (*p == '=') {
        p++;
        os_env_set(key, p);
        return 0;
      }
      /* Get single var */
      if (out_buf && out_buf_size > 0) {
        os_status_t st = os_env_get(args, out_buf, (unsigned int)out_buf_size);
        return (st == OS_STATUS_OK) ? 0 : 1;
      }
    } else {
      if (out_buf && out_buf_size > 0) {
        os_status_t st = os_env_list(out_buf, (unsigned int)out_buf_size);
        return (st == OS_STATUS_OK) ? 0 : 1;
      }
    }
    return 1;
  }

  if (sosh_app_streq(command, "date")) {
    if (out_buf && out_buf_size > 0) {
      os_status_t st = os_clock_get(out_buf, (unsigned int)out_buf_size);
      return (st == OS_STATUS_OK) ? 0 : 1;
    }
    return 1;
  }

  /* Unknown command — print error */
  if (out_buf && out_buf_size > 0) {
    sosh_app_strcpy(out_buf, "sosh: command not found: ", out_buf_size);
    sosh_app_strcat(out_buf, command, out_buf_size);
    sosh_app_strcat(out_buf, "\n", out_buf_size);
  }
  return 127;
}

/* --- Entry point ------------------------------------------------------- */

int main(void) {
  char args_buf[512];
  char script_path[256];
  char script_args[256];
  char script_content[8192];
  int script_len;
  int i, j;
  os_status_t st;

  /* Get our arguments from the OS */
  args_buf[0] = '\0';
  st = os_get_args(args_buf, sizeof(args_buf));
  if (st != OS_STATUS_OK || args_buf[0] == '\0') {
    os_console_write("Usage: sosh <script-path> [args...]\n");
    return 1;
  }

  /* Parse: first arg is script path, rest are script args */
  i = 0;
  while (args_buf[i] == ' ') i++;
  j = 0;
  while (args_buf[i] && args_buf[i] != ' ' && j < 255) {
    script_path[j++] = args_buf[i++];
  }
  script_path[j] = '\0';

  while (args_buf[i] == ' ') i++;
  sosh_app_strcpy(script_args, &args_buf[i], sizeof(script_args));

  /* Read script file */
  script_content[0] = '\0';
  st = os_fs_read_file(script_path, script_content, sizeof(script_content));
  if (st != OS_STATUS_OK) {
    os_console_write("sosh: cannot read script: ");
    os_console_write(script_path);
    os_console_write("\n");
    return 1;
  }

  script_len = sosh_app_strlen(script_content);

  /* Run the interpreter */
  {
    sosh_state_t state;
    sosh_eval_init(&state, sosh_app_output, sosh_app_exec, (void*)0);
    return sosh_eval_script(&state, script_content, script_len,
                            script_path, script_args);
  }
}
