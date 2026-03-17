/**
 * @file session_manager.c
 * @brief Multi-session management for the kernel console.
 *
 * Purpose:
 *   Manages multiple independent console sessions, each with its own
 *   context (working directory, environment variables, line buffer,
 *   loaded libraries).  Supports creating, switching, and listing
 *   sessions, as well as binding the active session to the console.
 *
 * Interactions:
 *   - console.c: each session owns a console_context_t that the console
 *     binds to when the session becomes active.
 *   - scheduler.c: session switches may interact with the cooperative
 *     scheduler to maintain per-session task state.
 *   - serial_hal.c: session status messages are written via the active
 *     serial backend for debug output.
 *
 * Launched by:
 *   session_manager_init() is called from kmain() during kernel boot.
 *   Not a standalone process; compiled into the kernel image.
 */

#include "session_manager.h"

#include "../hal/serial_hal.h"
#include "../sched/scheduler.h"
#include "console.h"

enum {
  SESSION_MAX = 8,
};

typedef struct {
  int in_use;
  unsigned int session_id;
  cap_subject_id_t subject_id;
  console_context_t console_context;
} session_record_t;

static session_record_t g_sessions[SESSION_MAX];
static unsigned int g_active_session_id;
static cap_subject_id_t g_default_subject_id;

static void session_copy_string(char *dst, size_t dst_size, const char *src) {
  size_t i = 0u;

  if (dst == 0 || dst_size == 0u) {
    return;
  }

  if (src == 0) {
    dst[0] = '\0';
    return;
  }

  while (src[i] != '\0' && i + 1u < dst_size) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

static void session_init_context(console_context_t *context, cap_subject_id_t subject_id) {
  size_t i = 0u;

  if (context == 0) {
    return;
  }

  context->subject_id = subject_id;
  context->line[0] = '\0';
  context->line_len = 0u;
  context->pending_line[0] = '\0';
  context->pending_line_len = 0u;
  context->history_count = 0u;
  context->history_next = 0u;
  context->history_browse_index = -1;
  context->screen_history[0] = '\0';
  context->screen_history_len = 0u;
  context->next_correlation_id = 1u;
  session_copy_string(context->cwd, sizeof(context->cwd), "/");
  context->escape_state = 0u;
  context->next_loaded_lib_handle = 1u;

  for (i = 0u; i < CONSOLE_HISTORY_MAX; ++i) {
    context->history[i][0] = '\0';
  }

  for (i = 0u; i < CONSOLE_ENV_MAX; ++i) {
    context->env_entries[i].used = 0;
    context->env_entries[i].key[0] = '\0';
    context->env_entries[i].value[0] = '\0';
  }

  for (i = 0u; i < CONSOLE_LOADED_LIB_MAX; ++i) {
    context->loaded_libs[i].used = 0;
    context->loaded_libs[i].handle = 0u;
    context->loaded_libs[i].program_len = 0u;
    context->loaded_libs[i].ref_count = 0u;
    context->loaded_libs[i].owner_session_id = 0u;
    context->loaded_libs[i].owner_subject_id = 0u;
    context->loaded_libs[i].owner_actor[0] = '\0';
    context->loaded_libs[i].path[0] = '\0';
  }

  context->env_entries[0].used = 1;
  session_copy_string(context->env_entries[0].key, sizeof(context->env_entries[0].key), "PWD");
  session_copy_string(context->env_entries[0].value, sizeof(context->env_entries[0].value), "/");
}

static void session_manager_reset(void) {
  size_t i = 0u;
  for (i = 0u; i < SESSION_MAX; ++i) {
    g_sessions[i].in_use = 0;
    g_sessions[i].session_id = 0u;
    g_sessions[i].subject_id = 0u;
    session_init_context(&g_sessions[i].console_context, 0u);
  }
  g_active_session_id = 0u;
}

static int session_manager_create_session(cap_subject_id_t subject_id) {
  size_t i = 0u;

  for (i = 0u; i < SESSION_MAX; ++i) {
    if (!g_sessions[i].in_use) {
      g_sessions[i].in_use = 1;
      g_sessions[i].session_id = (unsigned int)i;
      g_sessions[i].subject_id = subject_id;
      session_init_context(&g_sessions[i].console_context, subject_id);
      return (int)i;
    }
  }

  return -1;
}

static int session_manager_create_session_at(unsigned int session_id, cap_subject_id_t subject_id) {
  if (session_id >= SESSION_MAX || g_sessions[session_id].in_use) {
    return -1;
  }

  g_sessions[session_id].in_use = 1;
  g_sessions[session_id].session_id = session_id;
  g_sessions[session_id].subject_id = subject_id;
  session_init_context(&g_sessions[session_id].console_context, subject_id);
  return (int)session_id;
}

static void session_manager_console_task(void *context) {
  session_record_t *session = (session_record_t *)context;
  if (session == 0) {
    return;
  }

  console_init(&session->console_context, session->subject_id);
  console_run();
}

void session_manager_start(cap_subject_id_t bootstrap_subject_id) {
  int session_id = -1;

  serial_hal_write("TEST:START:session_manager\n");

  session_manager_reset();
  g_default_subject_id = bootstrap_subject_id;
  session_id = session_manager_create_session(bootstrap_subject_id);
  if (session_id < 0) {
    serial_hal_write("TEST:FAIL:session_manager:create_session\n");
    return;
  }

  g_active_session_id = (unsigned int)session_id;
  if (sched_spawn("session0-console", session_manager_console_task, &g_sessions[session_id]) < 0) {
    serial_hal_write("TEST:FAIL:session_manager:spawn_console\n");
    return;
  }

  serial_hal_write("TEST:PASS:session_manager\n");
  sched_run_forever();
}

int session_manager_create(cap_subject_id_t subject_id, unsigned int *out_session_id) {
  int session_id = session_manager_create_session(subject_id);
  if (session_id < 0) {
    return 0;
  }

  if (out_session_id != 0) {
    *out_session_id = (unsigned int)session_id;
  }
  return 1;
}

int session_manager_switch(unsigned int session_id) {
  if (session_id >= SESSION_MAX) {
    return 0;
  }

  if (!g_sessions[session_id].in_use) {
    if (session_id >= 4u || session_manager_create_session_at(session_id, g_default_subject_id) < 0) {
      return 0;
    }
  }

  g_active_session_id = session_id;
  console_bind_context(&g_sessions[session_id].console_context);
  return 1;
}

unsigned int session_manager_active_id(void) {
  return g_active_session_id;
}

size_t session_manager_list(char *out_buffer, size_t out_buffer_size) {
  size_t i = 0u;
  size_t cursor = 0u;

  if (out_buffer == 0 || out_buffer_size == 0u) {
    return 0u;
  }

  out_buffer[0] = '\0';
  for (i = 0u; i < SESSION_MAX; ++i) {
    if (!g_sessions[i].in_use) {
      continue;
    }

    if (cursor + 9u >= out_buffer_size) {
      break;
    }

    out_buffer[cursor++] = 's';
    out_buffer[cursor++] = 'e';
    out_buffer[cursor++] = 's';
    out_buffer[cursor++] = 's';
    out_buffer[cursor++] = 'i';
    out_buffer[cursor++] = 'o';
    out_buffer[cursor++] = 'n';
    out_buffer[cursor++] = ' ';
    out_buffer[cursor++] = (char)('0' + (char)i);
    if (i == g_active_session_id && cursor + 9u < out_buffer_size) {
      out_buffer[cursor++] = ' ';
      out_buffer[cursor++] = '(';
      out_buffer[cursor++] = 'a';
      out_buffer[cursor++] = 'c';
      out_buffer[cursor++] = 't';
      out_buffer[cursor++] = 'i';
      out_buffer[cursor++] = 'v';
      out_buffer[cursor++] = 'e';
      out_buffer[cursor++] = ')';
    }
    if (cursor + 1u < out_buffer_size) {
      out_buffer[cursor++] = '\n';
    }
  }

  if (cursor < out_buffer_size) {
    out_buffer[cursor] = '\0';
  }
  return cursor;
}
