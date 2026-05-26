/**
 * @file mouse_hal.c
 * @brief Hardware Abstraction Layer for mouse input devices.
 *
 * Purpose:
 *   Tracks mouse cursor position, button state, and generates click events.
 *   Polls the PS/2 mouse driver for movement data and clamps the cursor
 *   within screen bounds. Manages a small event queue for consumers.
 *   Coordinates with the VGA text driver to render a visible cursor overlay.
 *
 * Interactions:
 *   - drivers/input/ps2_mouse.c: polled for raw mouse packets.
 *   - drivers/video/vga_text.c: calls vga_text_mouse_cursor_* to
 *     show/hide the text-mode cursor overlay.
 *   - core/console.c: calls mouse_hal_update() in the main loop.
 *   - user/libs/mouselib: queries state via kernel API stubs.
 *
 * Launched by:
 *   mouse_hal_init() is called from kmain during boot.
 *   Not a standalone process; compiled into kernel image.
 */

#include "mouse_hal.h"

#include "../drivers/input/ps2_mouse.h"

/* Text-mode screen defaults */
#define DEFAULT_WIDTH  80
#define DEFAULT_HEIGHT 25

/* Event queue size */
#define MOUSE_EVENT_QUEUE_MAX 16

static int g_mouse_available;
static int g_cursor_x;
static int g_cursor_y;
static int g_screen_width;
static int g_screen_height;
static unsigned char g_buttons;
static unsigned char g_prev_buttons;

/* Sub-cell accumulator for smooth movement.
 * PS/2 reports pixel-level deltas; we accumulate and only move the cursor
 * when enough movement has built up to cross a cell boundary.
 * In text mode (80x25) we use a scale of 8, in graphics mode (320x200)
 * we use 1 for direct pixel mapping. */
#define MOUSE_SCALE_TEXT 8
#define MOUSE_SCALE_GFX  1
static int g_scale_factor;
static int g_accum_x;
static int g_accum_y;

/* Simple ring buffer for events */
static mouse_event_t g_event_queue[MOUSE_EVENT_QUEUE_MAX];
static int g_event_head;
static int g_event_tail;
static int g_event_count;

static void mouse_enqueue_event(mouse_event_type_t type, int x, int y,
                                unsigned char button) {
  if (g_event_count >= MOUSE_EVENT_QUEUE_MAX) {
    /* Drop oldest event */
    g_event_tail = (g_event_tail + 1) % MOUSE_EVENT_QUEUE_MAX;
    g_event_count--;
  }

  mouse_event_t *ev = &g_event_queue[g_event_head];
  ev->type = type;
  ev->x = x;
  ev->y = y;
  ev->button = button;
  g_event_head = (g_event_head + 1) % MOUSE_EVENT_QUEUE_MAX;
  g_event_count++;
}

int mouse_hal_init(void) {
  g_mouse_available = 0;
  g_cursor_x = 0;
  g_cursor_y = 0;
  g_accum_x = 0;
  g_accum_y = 0;
  g_scale_factor = MOUSE_SCALE_TEXT;
  g_screen_width = DEFAULT_WIDTH;
  g_screen_height = DEFAULT_HEIGHT;
  g_buttons = 0;
  g_prev_buttons = 0;
  g_event_head = 0;
  g_event_tail = 0;
  g_event_count = 0;

  if (ps2_mouse_init()) {
    g_mouse_available = 1;
  }

  return g_mouse_available;
}

int mouse_hal_available(void) {
  return g_mouse_available;
}

/* Bounded drain limit per mouse_hal_update() call.
 *
 * Background (issue #337): ps2_mouse_poll() consumes at most one byte of PS/2
 * controller data per call (it spans 3 bytes per packet across 3 invocations).
 * Callers that only invoke mouse_hal_update() once per frame (notably the
 * window-manager render loop in user/apps/win, where the compositor + per-line
 * VFB read syscalls drop the achievable frame rate well below the ~100 Hz
 * PS/2 sample rate) accumulate an ever-growing backlog of unread bytes,
 * which is observed as severe cursor lag (#337) — while tight-loop callers
 * such as draw.bin stay responsive simply because they re-poll fast enough.
 *
 * Fix: drain the controller's available bytes per call, bounded so we never
 * monopolise the CPU if a stuck device floods data. The cap is generous
 * enough to absorb a full frame's worth of packets at typical PS/2 rates
 * (100 Hz * 3 bytes = 300 bytes/s; even a 5 Hz frame is ~60 bytes per
 * frame). MOUSE_DRAIN_MAX is set to 64 (≈21 complete packets) — well above
 * what a frame can realistically accrue, while still preserving a hard
 * upper bound for determinism.
 */
#define MOUSE_DRAIN_MAX 64

static void mouse_apply_packet(const ps2_mouse_event_t *raw) {
  /* Accumulate sub-cell movement and only move when threshold crossed */
  g_accum_x += raw->dx;
  g_accum_y += raw->dy;

  int cell_dx = g_accum_x / g_scale_factor;
  int cell_dy = g_accum_y / g_scale_factor;

  if (cell_dx != 0 || cell_dy != 0) {
    /* Consume the accumulated movement that resulted in cell moves */
    g_accum_x -= cell_dx * g_scale_factor;
    g_accum_y -= cell_dy * g_scale_factor;

    /* Update position, clamped to screen bounds */
    g_cursor_x += cell_dx;
    g_cursor_y += cell_dy;

    if (g_cursor_x < 0) g_cursor_x = 0;
    if (g_cursor_y < 0) g_cursor_y = 0;
    if (g_cursor_x >= g_screen_width) g_cursor_x = g_screen_width - 1;
    if (g_cursor_y >= g_screen_height) g_cursor_y = g_screen_height - 1;

    /* Enqueue move event */
    mouse_enqueue_event(MOUSE_EVENT_MOVE, g_cursor_x, g_cursor_y, 0);
  }

  /* Detect button state changes */
  g_prev_buttons = g_buttons;
  g_buttons = raw->buttons;

  unsigned char changed = g_buttons ^ g_prev_buttons;
  if (changed) {
    unsigned char mask;
    for (mask = 0x01u; mask <= 0x04u; mask <<= 1) {
      if (changed & mask) {
        if (g_buttons & mask) {
          mouse_enqueue_event(MOUSE_EVENT_BUTTON_DOWN, g_cursor_x,
                              g_cursor_y, mask);
        } else {
          mouse_enqueue_event(MOUSE_EVENT_BUTTON_UP, g_cursor_x,
                              g_cursor_y, mask);
        }
      }
    }
  }
}

void mouse_hal_update(void) {
  if (!g_mouse_available) {
    return;
  }

  /* Drain up to MOUSE_DRAIN_MAX bytes of pending PS/2 data per call so that
   * low-frame-rate callers (e.g. window-manager compositor loop, #337) do
   * not let the controller queue overflow and stall cursor movement. */
  int drained;
  for (drained = 0; drained < MOUSE_DRAIN_MAX; drained++) {
    ps2_mouse_event_t raw;
    if (!ps2_mouse_poll(&raw)) {
      break; /* no complete packet (yet) or no data available */
    }
    mouse_apply_packet(&raw);
  }
}

void mouse_hal_get_state(mouse_state_t *out_state) {
  if (out_state == 0) {
    return;
  }

  if (!g_mouse_available) {
    out_state->x = 0;
    out_state->y = 0;
    out_state->buttons = 0;
    return;
  }

  out_state->x = g_cursor_x;
  out_state->y = g_cursor_y;
  out_state->buttons = g_buttons;
}

int mouse_hal_poll_event(mouse_event_t *out_event) {
  if (out_event == 0 || g_event_count == 0) {
    return 0;
  }

  *out_event = g_event_queue[g_event_tail];
  g_event_tail = (g_event_tail + 1) % MOUSE_EVENT_QUEUE_MAX;
  g_event_count--;
  return 1;
}

void mouse_hal_set_bounds(int width, int height) {
  if (width > 0) g_screen_width = width;
  if (height > 0) g_screen_height = height;

  /* Use direct pixel mapping for graphics modes, scaled for text */
  if (g_screen_width > 80) {
    g_scale_factor = MOUSE_SCALE_GFX;
  } else {
    g_scale_factor = MOUSE_SCALE_TEXT;
  }

  /* Reset accumulators when switching modes */
  g_accum_x = 0;
  g_accum_y = 0;

  /* Re-clamp cursor if bounds shrunk */
  if (g_cursor_x >= g_screen_width) g_cursor_x = g_screen_width - 1;
  if (g_cursor_y >= g_screen_height) g_cursor_y = g_screen_height - 1;
}
