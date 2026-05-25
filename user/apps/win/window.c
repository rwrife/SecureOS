/**
 * @file window.c
 * @brief Window state management implementation for the SecureOS window manager.
 *
 * Purpose:
 *   Manages the window table (up to WIN_MAX_WINDOWS), handles window creation,
 *   destruction, focus management, hit-testing, and terminal text buffer logic
 *   (character append, newline, scrolling).
 *
 * Interactions:
 *   - window.h: public API.
 *   - main.c: creates/destroys windows.
 *   - input.c: hit-tests to determine drag/focus targets.
 *   - compositor.c: reads window state for rendering.
 *
 * Launched by:
 *   Not standalone. Compiled into win.bin application.
 */

#include "window.h"

static win_window_t g_windows[WIN_MAX_WINDOWS];

static void str_copy(char *dst, int dst_size, const char *src) {
  int i = 0;
  if (dst == 0 || dst_size <= 0) return;
  if (src == 0) { dst[0] = '\0'; return; }
  while (src[i] != '\0' && i + 1 < dst_size) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

void win_init(void) {
  int i, r;
  for (i = 0; i < WIN_MAX_WINDOWS; i++) {
    g_windows[i].active = 0;
    g_windows[i].focused = 0;
    g_windows[i].z_order = 0;
    g_windows[i].session_id = 0;
    g_windows[i].cursor_col = 0;
    g_windows[i].cursor_row = 0;
    for (r = 0; r < WIN_CONTENT_ROWS; r++) {
      g_windows[i].text[r][0] = '\0';
    }
  }
}

win_window_t *win_create(int x, int y, const char *title, unsigned int session_id) {
  int i;
  for (i = 0; i < WIN_MAX_WINDOWS; i++) {
    if (!g_windows[i].active) {
      int r;
      g_windows[i].active = 1;
      g_windows[i].x = x;
      g_windows[i].y = y;
      /* content area pixel size + title bar + border */
      g_windows[i].width = WIN_CONTENT_COLS * 6 + WIN_BORDER * 2;
      g_windows[i].height = WIN_TITLE_HEIGHT + WIN_CONTENT_ROWS * 8 + WIN_BORDER * 2;
      g_windows[i].session_id = session_id;
      g_windows[i].cursor_col = 0;
      g_windows[i].cursor_row = 0;
      g_windows[i].focused = 0;
      g_windows[i].z_order = i;
      str_copy(g_windows[i].title, 32, title);
      for (r = 0; r < WIN_CONTENT_ROWS; r++) {
        g_windows[i].text[r][0] = '\0';
      }
      return &g_windows[i];
    }
  }
  return 0;
}

void win_destroy(win_window_t *w) {
  if (w != 0) {
    w->active = 0;
    w->focused = 0;
  }
}

win_hit_zone_t win_hit_test(int mx, int my, win_window_t **out_win) {
  int i;
  int best_z = -1;
  win_window_t *hit = 0;
  win_hit_zone_t zone = WIN_HIT_NONE;

  /* Check all windows, prefer highest z_order */
  for (i = 0; i < WIN_MAX_WINDOWS; i++) {
    if (!g_windows[i].active) continue;
    if (mx >= g_windows[i].x && mx < g_windows[i].x + g_windows[i].width &&
        my >= g_windows[i].y && my < g_windows[i].y + g_windows[i].height) {
      if (g_windows[i].z_order > best_z) {
        best_z = g_windows[i].z_order;
        hit = &g_windows[i];
      }
    }
  }

  if (hit != 0) {
    int rel_y = my - hit->y;
    int rel_x = mx - hit->x;

    if (rel_y < WIN_TITLE_HEIGHT) {
      /* Check close button (top-right corner, 8x8 area) */
      if (rel_x >= hit->width - 10) {
        zone = WIN_HIT_CLOSE_BTN;
      } else {
        zone = WIN_HIT_TITLEBAR;
      }
    } else {
      zone = WIN_HIT_CONTENT;
    }
  }

  if (out_win != 0) *out_win = hit;
  return zone;
}

void win_set_focus(win_window_t *w) {
  int i;
  for (i = 0; i < WIN_MAX_WINDOWS; i++) {
    g_windows[i].focused = 0;
  }
  if (w != 0 && w->active) {
    w->focused = 1;
    /* Bring to front: give highest z_order */
    int max_z = 0;
    for (i = 0; i < WIN_MAX_WINDOWS; i++) {
      if (g_windows[i].active && g_windows[i].z_order > max_z) {
        max_z = g_windows[i].z_order;
      }
    }
    w->z_order = max_z + 1;
  }
}

win_window_t *win_get_focused(void) {
  int i;
  for (i = 0; i < WIN_MAX_WINDOWS; i++) {
    if (g_windows[i].active && g_windows[i].focused) {
      return &g_windows[i];
    }
  }
  return 0;
}

win_window_t *win_get_table(void) {
  return g_windows;
}

void win_move(win_window_t *w, int new_x, int new_y) {
  if (w == 0) return;
  w->x = new_x;
  w->y = new_y;
}

static void win_scroll_up(win_window_t *w) {
  int r, c;
  for (r = 0; r < WIN_CONTENT_ROWS - 1; r++) {
    for (c = 0; c <= WIN_CONTENT_COLS; c++) {
      w->text[r][c] = w->text[r + 1][c];
    }
  }
  w->text[WIN_CONTENT_ROWS - 1][0] = '\0';
}

void win_putchar(win_window_t *w, char ch) {
  if (w == 0) return;

  if (ch == '\n') {
    w->cursor_col = 0;
    w->cursor_row++;
    if (w->cursor_row >= WIN_CONTENT_ROWS) {
      win_scroll_up(w);
      w->cursor_row = WIN_CONTENT_ROWS - 1;
    }
    return;
  }

  if (ch == '\r') {
    w->cursor_col = 0;
    return;
  }

  if (w->cursor_col < WIN_CONTENT_COLS) {
    w->text[w->cursor_row][w->cursor_col] = ch;
    w->text[w->cursor_row][w->cursor_col + 1] = '\0';
    w->cursor_col++;
  }

  if (w->cursor_col >= WIN_CONTENT_COLS) {
    w->cursor_col = 0;
    w->cursor_row++;
    if (w->cursor_row >= WIN_CONTENT_ROWS) {
      win_scroll_up(w);
      w->cursor_row = WIN_CONTENT_ROWS - 1;
    }
  }
}

void win_puts(win_window_t *w, const char *str) {
  if (w == 0 || str == 0) return;
  while (*str != '\0') {
    win_putchar(w, *str);
    str++;
  }
}
