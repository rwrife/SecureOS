/**
 * @file window.h
 * @brief Window state management for the SecureOS window manager.
 *
 * Purpose:
 *   Defines the win_window_t structure representing a single on-screen window
 *   and provides functions for creating, destroying, moving, and hit-testing
 *   windows. Each window wraps a terminal session.
 *
 * Interactions:
 *   - main.c: creates/destroys windows, queries focus and position.
 *   - compositor.c: reads window state to render onto back-buffer.
 *   - input.c: uses hit-testing to determine drag/focus targets.
 *
 * Launched by:
 *   Not standalone. Compiled into win.bin application.
 */

#ifndef WIN_WINDOW_H
#define WIN_WINDOW_H

#define WIN_MAX_WINDOWS    4
#define WIN_TITLE_HEIGHT   10
#define WIN_BORDER         1
#define WIN_CONTENT_COLS   36
#define WIN_CONTENT_ROWS   16

typedef struct {
  int active;
  int x, y;
  int width, height;
  char title[32];
  unsigned int session_id;
  /* Text buffer: one char per cell */
  char text[WIN_CONTENT_ROWS][WIN_CONTENT_COLS + 1];
  int cursor_col, cursor_row;
  int focused;
  int z_order;
} win_window_t;

typedef enum {
  WIN_HIT_NONE = 0,
  WIN_HIT_TITLEBAR,
  WIN_HIT_CONTENT,
  WIN_HIT_CLOSE_BTN,
} win_hit_zone_t;

/** Initialize the window table. Must be called once at startup. */
void win_init(void);

/** Create a new window at (x,y) with the given title and session.
 *  Returns pointer to window or 0 if table is full. */
win_window_t *win_create(int x, int y, const char *title, unsigned int session_id);

/** Destroy a window, freeing its slot. */
void win_destroy(win_window_t *w);

/** Hit-test: which zone of which window does (mx, my) fall in?
 *  Returns the window pointer via *out_win (or 0 if no hit). */
win_hit_zone_t win_hit_test(int mx, int my, win_window_t **out_win);

/** Set focus to the given window (unfocuses all others). */
void win_set_focus(win_window_t *w);

/** Get the currently focused window (or 0 if none). */
win_window_t *win_get_focused(void);

/** Get window table for iteration. Returns array of WIN_MAX_WINDOWS entries. */
win_window_t *win_get_table(void);

/** Move a window to new coordinates. */
void win_move(win_window_t *w, int new_x, int new_y);

/** Append a character to the window's terminal buffer (handles newline, scroll). */
void win_putchar(win_window_t *w, char ch);

/** Append a string to the window's terminal buffer. */
void win_puts(win_window_t *w, const char *str);

#endif /* WIN_WINDOW_H */
