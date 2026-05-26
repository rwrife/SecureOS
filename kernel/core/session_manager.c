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

#include "ctx_switch.h"
#include "../gfx/vfb_font.h"
#include "../hal/serial_hal.h"
#include "../mem/kheap.h"
#include "../sched/scheduler.h"
#include "console.h"

enum {
  SESSION_MAX = 8,
  SESSION_VFB_WIDTH = 320,
  SESSION_VFB_HEIGHT = 200,
  SESSION_VFB_SIZE = 64000, /* 320 * 200 */
  /* Text grid dimensions for VFB text rendering.
   * Must match the window content area so scrolling triggers at the right time.
   * Window content: 216px wide (36*6), 128px tall (16*8). */
  SESSION_VFB_TEXT_COLS = 36,  /* 216 / (5+1) = 36 */
  SESSION_VFB_TEXT_ROWS = 16,  /* 128 / (7+1) = 16 */
};

#define VFB_FG_COLOR 15  /* white */
#define VFB_BG_COLOR 0   /* black */

/* Each WM-managed session gets its own 16KB stack for tick execution
 * so that yielded frames aren't corrupted by the WM's main loop. */
#define SESSION_TICK_STACK_SIZE 16384

/* ELF memory region save/restore — all user apps share the same load address
 * (0x800000). When a child app runs inside a tick, it overwrites the parent
 * app's code. We save/restore around tick boundaries. */
#define ELF_REGION_START  0x00800000u
#define ELF_SNAPSHOT_SIZE (128u * 1024u)  /* 128KB — covers all current apps */

typedef struct {
  int in_use;
  unsigned int session_id;
  cap_subject_id_t subject_id;
  console_context_t console_context;
  /* Virtual framebuffer — always allocated for WM-managed sessions */
  int gfx_mode;                     /* 0 = text, 1 = graphics */
  unsigned char *vfb;               /* Allocated via kmalloc when needed */
  /* Text cursor for kernel-side text rendering into VFB */
  int vfb_cursor_col;
  int vfb_cursor_row;
  /* Virtual mouse state — injected by WM, read by child apps */
  int virtual_mouse_x;
  int virtual_mouse_y;
  unsigned char virtual_mouse_buttons;
  /* Cooperative yield state for auth spin-wait */
  int blocked;                      /* 1 if yielded mid-command */
  ctx_jmp_buf_t blocked_ctx;        /* Saved context at idle_wait point */
  unsigned char *tick_stack;        /* Dedicated stack for tick execution */
  /* ELF snapshot — saved code/data for child app between yields */
  unsigned char *elf_snapshot;      /* Allocated when child ELF first runs */
} session_record_t;

/* Static buffer to save the parent app's ELF region during a tick.
 * Only one tick runs at a time (cooperative), so one buffer suffices. */
static unsigned char g_elf_parent_buf[ELF_SNAPSHOT_SIZE];

static session_record_t g_sessions[SESSION_MAX];
static unsigned int g_active_session_id;
static cap_subject_id_t g_default_subject_id;

/* Global yield context: tick saves here so idle_wait can return to it */
static ctx_jmp_buf_t g_tick_return_ctx;
static unsigned int g_tick_session_id;  /* session currently being ticked */

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
  context->screen_history_read_cursor = 0u;
  context->next_correlation_id = 1u;
  session_copy_string(context->cwd, sizeof(context->cwd), "/");
  context->escape_state = 0u;
  context->next_loaded_lib_handle = 1u;
  context->inject_buf[0] = '\0';
  context->inject_head = 0u;
  context->inject_tail = 0u;
  context->wm_managed = 0;

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
    g_sessions[i].blocked = 0;
    session_init_context(&g_sessions[i].console_context, 0u);
  }
  g_active_session_id = 0u;
  g_tick_session_id = SESSION_MAX;
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

  /* Initialize the console context so the session is ready for use.
   * console_init clobbers the global context pointer, so save/restore. */
  {
    console_context_t *saved_ctx = 0;
    /* The active session's context needs to be preserved */
    if (g_active_session_id < SESSION_MAX && g_sessions[g_active_session_id].in_use) {
      saved_ctx = &g_sessions[g_active_session_id].console_context;
    }
    console_init(&g_sessions[session_id].console_context, subject_id);
    if (saved_ctx != 0) {
      console_bind_context(saved_ctx);
    }
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

size_t session_manager_read_output(unsigned int session_id, char *out_buffer,
                                   size_t out_buffer_size) {
  console_context_t *ctx;
  size_t available;
  size_t to_copy;
  size_t i;

  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    return 0u;
  }
  if (out_buffer == 0 || out_buffer_size == 0u) {
    return 0u;
  }

  ctx = &g_sessions[session_id].console_context;
  available = ctx->screen_history_len - ctx->screen_history_read_cursor;
  if (available == 0u) {
    out_buffer[0] = '\0';
    return 0u;
  }

  to_copy = available;
  if (to_copy >= out_buffer_size) {
    to_copy = out_buffer_size - 1u;
  }

  for (i = 0u; i < to_copy; ++i) {
    out_buffer[i] = ctx->screen_history[ctx->screen_history_read_cursor + i];
  }
  out_buffer[to_copy] = '\0';
  ctx->screen_history_read_cursor += to_copy;

  return to_copy;
}

size_t session_manager_write_input(unsigned int session_id, const char *input,
                                   size_t len) {
  console_context_t *ctx;
  size_t injected = 0u;
  size_t i;

  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    return 0u;
  }
  if (input == 0 || len == 0u) {
    return 0u;
  }

  ctx = &g_sessions[session_id].console_context;

  for (i = 0u; i < len; ++i) {
    size_t next_head = (ctx->inject_head + 1u) % CONSOLE_LINE_MAX;
    if (next_head == ctx->inject_tail) {
      break; /* buffer full */
    }
    ctx->inject_buf[ctx->inject_head] = input[i];
    ctx->inject_head = next_head;
    ++injected;
  }

  return injected;
}

/* Wrapper that runs console_process_injected on the session's tick stack.
 * When it completes (or if it never yields), jump back to the tick return
 * point so we don't rely on the stale stack frame from ctx_call_on_stack. */
static void tick_trampoline(void) {
  console_process_injected();
  /* Command completed without yielding (or after resume completed).
   * Jump back to the tick's ctx_save point so we exit cleanly. */
  ctx_resume(&g_tick_return_ctx, 2);
}

void session_manager_tick(unsigned int session_id) {
  unsigned int prev_active;
  int save_result;
  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    return;
  }

  /* Allocate tick stack on first use */
  if (g_sessions[session_id].tick_stack == 0 &&
      g_sessions[session_id].console_context.wm_managed) {
    g_sessions[session_id].tick_stack =
        (unsigned char *)kmalloc(SESSION_TICK_STACK_SIZE);
    if (g_sessions[session_id].tick_stack == 0) {
      return; /* Cannot tick without a stack */
    }
  }

  /* Non-WM sessions: just process injected input directly */
  if (!g_sessions[session_id].console_context.wm_managed) {
    prev_active = g_active_session_id;
    g_active_session_id = session_id;
    g_tick_session_id = session_id;
    console_bind_context(&g_sessions[session_id].console_context);
    console_process_injected();
    g_active_session_id = prev_active;
    g_tick_session_id = SESSION_MAX;
    if (prev_active < SESSION_MAX && g_sessions[prev_active].in_use) {
      console_bind_context(&g_sessions[prev_active].console_context);
    }
    return;
  }

  /* --- ELF region save: protect parent app's code at 0x800000 ---
   * All user ELF apps share the same load address. A child app loaded
   * during this tick would overwrite the parent. Save the parent's code
   * now; restore it when the tick yields or completes. */
  {
    unsigned char *elf_base = (unsigned char *)(unsigned long)ELF_REGION_START;
    unsigned int i;
    for (i = 0; i < ELF_SNAPSHOT_SIZE; i++) {
      g_elf_parent_buf[i] = elf_base[i];
    }
  }

  /* Temporarily switch active session so console_write's VFB hook
   * targets the correct session during tick processing */
  prev_active = g_active_session_id;
  g_active_session_id = session_id;
  g_tick_session_id = session_id;

  /* Bind the target session's context */
  console_bind_context(&g_sessions[session_id].console_context);

  /* Save the "return to WM" context.
   * Returns: 0 = initial save, 1 = yield (session blocked), 2 = completed */
  save_result = ctx_save(&g_tick_return_ctx);
  if (save_result == 1) {
    /* Yielded from console_idle_wait — session is blocked on auth */
    g_sessions[session_id].blocked = 1;
    goto restore;
  }
  if (save_result == 2) {
    /* tick_trampoline completed — command finished normally */
    goto restore;
  }

  if (g_sessions[session_id].blocked) {
    /* Session was previously yielded (blocked).
     * Restore the child app's ELF snapshot before resuming. */
    if (g_sessions[session_id].elf_snapshot != 0) {
      unsigned char *elf_base = (unsigned char *)(unsigned long)ELF_REGION_START;
      unsigned int i;
      for (i = 0; i < ELF_SNAPSHOT_SIZE; i++) {
        elf_base[i] = g_sessions[session_id].elf_snapshot[i];
      }
    }
    g_sessions[session_id].blocked = 0;
    ctx_resume(&g_sessions[session_id].blocked_ctx, 1);
    /* Does not return */
  }

  /* Normal path: run on the session's dedicated tick stack */
  {
    unsigned char *stack_top = g_sessions[session_id].tick_stack +
                               SESSION_TICK_STACK_SIZE;
    /* Align to 16 bytes */
    stack_top = (unsigned char *)((unsigned long)stack_top & ~0xFUL);
    ctx_call_on_stack(stack_top, tick_trampoline);
    /* Should not reach here — tick_trampoline always ctx_resumes back.
     * But if it somehow does, fall through to restore. */
  }

restore:
  /* --- ELF region restore: save child's code, bring back parent's ---
   * Save child app's current ELF state (for future resume), then restore
   * the parent app's code so it can continue executing. */
  {
    unsigned char *elf_base = (unsigned char *)(unsigned long)ELF_REGION_START;
    unsigned int i;

    /* Allocate child snapshot on first use */
    if (g_sessions[session_id].elf_snapshot == 0) {
      g_sessions[session_id].elf_snapshot =
          (unsigned char *)kmalloc(ELF_SNAPSHOT_SIZE);
    }

    /* Save child's ELF state */
    if (g_sessions[session_id].elf_snapshot != 0) {
      for (i = 0; i < ELF_SNAPSHOT_SIZE; i++) {
        g_sessions[session_id].elf_snapshot[i] = elf_base[i];
      }
    }

    /* Restore parent's ELF code */
    for (i = 0; i < ELF_SNAPSHOT_SIZE; i++) {
      elf_base[i] = g_elf_parent_buf[i];
    }
  }

  /* Restore previous active session and context */
  g_active_session_id = prev_active;
  g_tick_session_id = SESSION_MAX; /* invalid = not in tick */
  if (prev_active < SESSION_MAX && g_sessions[prev_active].in_use) {
    console_bind_context(&g_sessions[prev_active].console_context);
  }
}

void session_manager_set_wm_managed(unsigned int session_id, int managed) {
  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    return;
  }
  g_sessions[session_id].console_context.wm_managed = managed;

  /* Eagerly allocate VFB when marking as WM-managed */
  if (managed && g_sessions[session_id].vfb == 0) {
    serial_hal_write("[wm] allocating VFB for session\n");
    g_sessions[session_id].vfb =
        (unsigned char *)kmalloc((size_t)SESSION_VFB_SIZE);
    g_sessions[session_id].vfb_cursor_col = 0;
    g_sessions[session_id].vfb_cursor_row = 0;
    if (g_sessions[session_id].vfb != 0) {
      /* Zero the buffer */
      unsigned int bi;
      for (bi = 0; bi < SESSION_VFB_SIZE; bi++) {
        g_sessions[session_id].vfb[bi] = 0;
      }
      /* Render an initial prompt so the user sees something */
      session_manager_vfb_write(session_id, "[s");
      session_manager_vfb_putchar(session_id, '0' + (char)(session_id % 10));
      session_manager_vfb_write(session_id, " /]> ");
    }
    serial_hal_write("[wm] VFB allocated\n");
  }
}

int session_manager_is_wm_managed(unsigned int session_id) {
  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    return 0;
  }
  return g_sessions[session_id].console_context.wm_managed;
}

int session_manager_get_gfx_mode(unsigned int session_id) {
  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    return -1;
  }
  return g_sessions[session_id].gfx_mode;
}

void session_manager_set_gfx_mode(unsigned int session_id, int gfx_mode) {
  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    return;
  }
  g_sessions[session_id].gfx_mode = gfx_mode;
}

void session_manager_set_virtual_mouse(unsigned int session_id,
                                       int x, int y,
                                       unsigned char buttons) {
  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    return;
  }
  g_sessions[session_id].virtual_mouse_x = x;
  g_sessions[session_id].virtual_mouse_y = y;
  g_sessions[session_id].virtual_mouse_buttons = buttons;
}

void session_manager_get_virtual_mouse(unsigned int session_id,
                                       int *out_x, int *out_y,
                                       unsigned char *out_buttons) {
  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    if (out_buttons) *out_buttons = 0;
    return;
  }
  if (out_x) *out_x = g_sessions[session_id].virtual_mouse_x;
  if (out_y) *out_y = g_sessions[session_id].virtual_mouse_y;
  if (out_buttons) *out_buttons = g_sessions[session_id].virtual_mouse_buttons;
}

void session_manager_clear_vfb(unsigned int session_id) {
  unsigned char *vfb;
  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    return;
  }
  vfb = session_manager_get_vfb(session_id);
  if (vfb != 0) {
    unsigned int i;
    for (i = 0; i < SESSION_VFB_SIZE; i++) {
      vfb[i] = 0;
    }
  }
}

unsigned char *session_manager_get_vfb(unsigned int session_id) {
  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    return 0;
  }

  /* Lazy allocate the VFB on first access */
  if (g_sessions[session_id].vfb == 0) {
    g_sessions[session_id].vfb =
        (unsigned char *)kmalloc((size_t)SESSION_VFB_SIZE);
  }

  return g_sessions[session_id].vfb;
}

size_t session_manager_read_vfb(unsigned int session_id,
                                unsigned char *out_pixels,
                                unsigned int x, unsigned int y,
                                unsigned int w, unsigned int h) {
  unsigned char *vfb;
  unsigned int row;
  size_t written = 0u;

  if (out_pixels == 0 || w == 0u || h == 0u) {
    return 0u;
  }
  if (x >= SESSION_VFB_WIDTH || y >= SESSION_VFB_HEIGHT) {
    return 0u;
  }

  vfb = session_manager_get_vfb(session_id);
  if (vfb == 0) {
    return 0u;
  }

  /* Clip to VFB bounds */
  if (x + w > SESSION_VFB_WIDTH) {
    w = SESSION_VFB_WIDTH - x;
  }
  if (y + h > SESSION_VFB_HEIGHT) {
    h = SESSION_VFB_HEIGHT - y;
  }

  /* Copy row by row */
  for (row = 0u; row < h; ++row) {
    unsigned int src_offset = (y + row) * SESSION_VFB_WIDTH + x;
    unsigned int col;
    for (col = 0u; col < w; ++col) {
      out_pixels[written++] = vfb[src_offset + col];
    }
  }

  return written;
}

/* --- VFB text rendering for WM-managed sessions --- */

/**
 * Scroll the VFB up by one text line (VFB_FONT_H+1 pixels).
 * Moves all pixel rows up and clears the bottom line.
 */
static void vfb_scroll_up(unsigned char *vfb) {
  int line_h = VFB_FONT_H + VFB_FONT_SPACING;
  int shift_bytes = line_h * SESSION_VFB_WIDTH;
  int total_bytes = SESSION_VFB_SIZE;
  int i;

  /* Shift pixels up */
  for (i = 0; i < total_bytes - shift_bytes; i++) {
    vfb[i] = vfb[i + shift_bytes];
  }
  /* Clear the bottom area */
  for (i = total_bytes - shift_bytes; i < total_bytes; i++) {
    vfb[i] = VFB_BG_COLOR;
  }
}

void session_manager_vfb_putchar(unsigned int session_id, char ch) {
  session_record_t *s;
  unsigned char *vfb;
  int px, py;

  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    return;
  }
  s = &g_sessions[session_id];
  vfb = s->vfb;
  if (vfb == 0) {
    return;
  }

  if (ch == '\n') {
    s->vfb_cursor_col = 0;
    s->vfb_cursor_row++;
  } else if (ch == '\r') {
    s->vfb_cursor_col = 0;
  } else if (ch == '\b' || ch == 0x7F) {
    /* Backspace: move cursor back and clear the cell */
    if (s->vfb_cursor_col > 0) {
      s->vfb_cursor_col--;
      px = s->vfb_cursor_col * (VFB_FONT_W + VFB_FONT_SPACING);
      py = s->vfb_cursor_row * (VFB_FONT_H + VFB_FONT_SPACING);
      vfb_font_draw_char(vfb, SESSION_VFB_WIDTH, px, py, ' ', VFB_BG_COLOR);
    }
  } else if (ch == '\t') {
    s->vfb_cursor_col = (s->vfb_cursor_col + 4) & ~3;
  } else {
    /* Render the character glyph at current cursor position */
    px = s->vfb_cursor_col * (VFB_FONT_W + VFB_FONT_SPACING);
    py = s->vfb_cursor_row * (VFB_FONT_H + VFB_FONT_SPACING);
    vfb_font_draw_char(vfb, SESSION_VFB_WIDTH, px, py, ch, VFB_FG_COLOR);
    s->vfb_cursor_col++;
  }

  /* Wrap at end of line */
  if (s->vfb_cursor_col >= SESSION_VFB_TEXT_COLS) {
    s->vfb_cursor_col = 0;
    s->vfb_cursor_row++;
  }

  /* Scroll if past last row */
  if (s->vfb_cursor_row >= SESSION_VFB_TEXT_ROWS) {
    vfb_scroll_up(vfb);
    s->vfb_cursor_row = SESSION_VFB_TEXT_ROWS - 1;
  }
}

void session_manager_vfb_write(unsigned int session_id, const char *text) {
  if (text == 0) return;
  while (*text != '\0') {
    session_manager_vfb_putchar(session_id, *text);
    text++;
  }
}

int session_manager_tick_yield(void) {
  /* Called from bridge input/mouse functions when a WM-managed session
   * needs to yield. Saves the blocked context and jumps back to
   * session_manager_tick. */
  if (g_tick_session_id >= SESSION_MAX) {
    return 0; /* Not inside a tick — cannot yield */
  }
  /* Save where we are */
  if (ctx_save(&g_sessions[g_tick_session_id].blocked_ctx) != 0) {
    /* Resumed! We're back from yield. */
    return 1;
  }
  /* Jump back to session_manager_tick's ctx_save(g_tick_return_ctx) */
  ctx_resume(&g_tick_return_ctx, 1);
  /* Never reached */
  return 0;
}

int session_manager_is_blocked(unsigned int session_id) {
  if (session_id >= SESSION_MAX || !g_sessions[session_id].in_use) {
    return 0;
  }
  return g_sessions[session_id].blocked;
}
