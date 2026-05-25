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
#include "../drivers/video/vga_text.h"
#include "video_hal.h"

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
 * when enough movement has built up to cross a cell boundary. */
#define MOUSE_SCALE_FACTOR 8
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
  g_screen_width = DEFAULT_WIDTH;
  g_screen_height = DEFAULT_HEIGHT;
  g_buttons = 0;
  g_prev_buttons = 0;
  g_event_head = 0;
  g_event_tail = 0;
  g_event_count = 0;

  if (ps2_mouse_init()) {
    g_mouse_available = 1;
    vga_text_mouse_cursor_show(g_cursor_x, g_cursor_y);
  }

  return g_mouse_available;
}

int mouse_hal_available(void) {
  return g_mouse_available;
}

void mouse_hal_update(void) {
  if (!g_mouse_available) {
    return;
  }

  ps2_mouse_event_t raw;
  if (!ps2_mouse_poll(&raw)) {
    return;
  }

  /* Accumulate sub-cell movement and only move when threshold crossed */
  g_accum_x += raw.dx;
  g_accum_y += raw.dy;

  int cell_dx = g_accum_x / MOUSE_SCALE_FACTOR;
  int cell_dy = g_accum_y / MOUSE_SCALE_FACTOR;

  if (cell_dx == 0 && cell_dy == 0) {
    /* Not enough movement to cross a cell boundary yet.
     * Still check for button changes below. */
    goto check_buttons;
  }

  /* Consume the accumulated movement that resulted in cell moves */
  g_accum_x -= cell_dx * MOUSE_SCALE_FACTOR;
  g_accum_y -= cell_dy * MOUSE_SCALE_FACTOR;

  /* Hide cursor at old position before moving */
  vga_text_mouse_cursor_hide();

  /* Update position, clamped to screen bounds */
  g_cursor_x += cell_dx;
  g_cursor_y += cell_dy;

  if (g_cursor_x < 0) g_cursor_x = 0;
  if (g_cursor_y < 0) g_cursor_y = 0;
  if (g_cursor_x >= g_screen_width) g_cursor_x = g_screen_width - 1;
  if (g_cursor_y >= g_screen_height) g_cursor_y = g_screen_height - 1;

  /* Enqueue move event */
  mouse_enqueue_event(MOUSE_EVENT_MOVE, g_cursor_x, g_cursor_y, 0);

  /* Show cursor at new position */
  vga_text_mouse_cursor_show(g_cursor_x, g_cursor_y);

check_buttons:
  /* Detect button state changes */
  g_prev_buttons = g_buttons;
  g_buttons = raw.buttons;

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

  /* Re-clamp cursor if bounds shrunk */
  if (g_cursor_x >= g_screen_width) g_cursor_x = g_screen_width - 1;
  if (g_cursor_y >= g_screen_height) g_cursor_y = g_screen_height - 1;
}
