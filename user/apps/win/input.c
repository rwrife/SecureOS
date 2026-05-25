/**
 * @file input.c
 * @brief Input dispatch implementation for the SecureOS window manager.
 *
 * Purpose:
 *   Handles mouse events (drag, click-to-focus, close) and keyboard input
 *   routing. Tracks drag state across frames. Sends keyboard characters to
 *   the focused window's session via os_session_write_input.
 *
 * Interactions:
 *   - input.h: public API.
 *   - window.h: hit-testing, focus, move.
 *   - secureos_api.h: mouse/keyboard polling, session write.
 *
 * Launched by:
 *   Not standalone. Compiled into win.bin application.
 */

#include "input.h"
#include "window.h"
#include "secureos_api.h"
#include "auth_dialog.h"

#define KEY_ESCAPE 0x1Bu

/* Drag state */
static int g_dragging;
static win_window_t *g_drag_window;
static int g_drag_offset_x;
static int g_drag_offset_y;

/* Previous button state for edge detection */
static unsigned char g_prev_buttons;

/* Expose mouse state for compositor cursor rendering */
static int g_mouse_x;
static int g_mouse_y;

int input_get_mouse_x(void) { return g_mouse_x; }
int input_get_mouse_y(void) { return g_mouse_y; }

void input_init(void) {
  g_dragging = 0;
  g_drag_window = 0;
  g_drag_offset_x = 0;
  g_drag_offset_y = 0;
  g_prev_buttons = 0;
  g_mouse_x = 160;
  g_mouse_y = 100;
}

int input_update(void) {
  int mx = 0, my = 0;
  unsigned char buttons = 0;
  char key = '\0';
  int left_pressed;
  int left_released;

  /* Poll keyboard */
  if (os_input_read_char(&key) == OS_STATUS_OK && key != '\0') {
    if (key == KEY_ESCAPE) {
      return 1; /* signal exit */
    }

    /* Route keyboard to focused window's session */
    win_window_t *focused = win_get_focused();
    if (focused != 0) {
      os_session_write_input(focused->session_id, &key, 1);
      /* Also echo to window's text buffer for v1 visual feedback */
      win_putchar(focused, key);
    }
  }

  /* Poll mouse */
  os_mouse_get_state(&mx, &my, &buttons);
  g_mouse_x = mx;
  g_mouse_y = my;

  /* Clamp to screen */
  if (g_mouse_x < 0) g_mouse_x = 0;
  if (g_mouse_x >= 320) g_mouse_x = 319;
  if (g_mouse_y < 0) g_mouse_y = 0;
  if (g_mouse_y >= 200) g_mouse_y = 199;

  left_pressed = (buttons & 0x01) && !(g_prev_buttons & 0x01);
  left_released = !(buttons & 0x01) && (g_prev_buttons & 0x01);

  if (left_pressed && !g_dragging) {
    /* Auth dialog takes priority over window interaction */
    if (auth_dialog_active() && auth_dialog_click(g_mouse_x, g_mouse_y)) {
      /* Click consumed by dialog */
    } else {
    win_window_t *hit_win = 0;
    win_hit_zone_t zone = win_hit_test(g_mouse_x, g_mouse_y, &hit_win);

    if (zone == WIN_HIT_CLOSE_BTN && hit_win != 0) {
      win_destroy(hit_win);
    } else if (zone == WIN_HIT_TITLEBAR && hit_win != 0) {
      /* Start drag */
      g_dragging = 1;
      g_drag_window = hit_win;
      g_drag_offset_x = g_mouse_x - hit_win->x;
      g_drag_offset_y = g_mouse_y - hit_win->y;
      win_set_focus(hit_win);
    } else if (zone == WIN_HIT_CONTENT && hit_win != 0) {
      win_set_focus(hit_win);
    }
    }
  }

  if (g_dragging && (buttons & 0x01)) {
    /* Continue drag */
    if (g_drag_window != 0) {
      win_move(g_drag_window,
               g_mouse_x - g_drag_offset_x,
               g_mouse_y - g_drag_offset_y);
    }
  }

  if (left_released && g_dragging) {
    g_dragging = 0;
    g_drag_window = 0;
  }

  g_prev_buttons = buttons;
  return 0;
}
