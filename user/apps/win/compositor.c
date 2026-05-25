/**
 * @file compositor.c
 * @brief Screen compositor implementation for the SecureOS window manager.
 *
 * Purpose:
 *   Renders all active windows into a 320x200 back-buffer in z-order
 *   (back-to-front), then flushes the result to the physical framebuffer.
 *   Each frame: clears background, draws each window (border, title bar,
 *   content text), then draws the mouse cursor overlay.
 *
 * Interactions:
 *   - compositor.h: public API.
 *   - window.h: iterates the window table for rendering.
 *   - font.h: renders text into the back-buffer.
 *   - secureos_api.h: os_video_blit to flush back-buffer to screen.
 *
 * Launched by:
 *   Not standalone. Compiled into win.bin application.
 */

#include "compositor.h"
#include "window.h"
#include "font.h"
#include "secureos_api.h"

#define SCREEN_W 320
#define SCREEN_H 200

/* VGA palette color indices */
#define COLOR_DESKTOP     1   /* dark blue */
#define COLOR_TITLEBAR    9   /* light blue */
#define COLOR_TITLEBAR_UF 8   /* dark grey (unfocused) */
#define COLOR_TITLE_TEXT  15  /* white */
#define COLOR_BORDER      7   /* light grey */
#define COLOR_CONTENT_BG  0   /* black */
#define COLOR_CONTENT_FG  15  /* white */
#define COLOR_CLOSE_BTN   4   /* red */
#define COLOR_CURSOR      15  /* white */

static unsigned char g_backbuffer[SCREEN_W * SCREEN_H];

static void fill_rect(int x, int y, int w, int h, unsigned char color) {
  int row, col;
  for (row = 0; row < h; row++) {
    int py = y + row;
    if (py < 0 || py >= SCREEN_H) continue;
    for (col = 0; col < w; col++) {
      int px = x + col;
      if (px < 0 || px >= SCREEN_W) continue;
      g_backbuffer[py * SCREEN_W + px] = color;
    }
  }
}

static void draw_window(win_window_t *w) {
  int content_x, content_y;
  int content_w, content_h;
  int row;
  unsigned char tb_color;
  int gfx_mode = 0;

  if (w == 0 || !w->active) return;

  /* Border */
  fill_rect(w->x, w->y, w->width, w->height, COLOR_BORDER);

  /* Title bar */
  tb_color = w->focused ? COLOR_TITLEBAR : COLOR_TITLEBAR_UF;
  fill_rect(w->x + WIN_BORDER, w->y + WIN_BORDER,
            w->width - WIN_BORDER * 2, WIN_TITLE_HEIGHT - WIN_BORDER,
            tb_color);

  /* Title text */
  font_draw_string(g_backbuffer, SCREEN_W,
                   w->x + WIN_BORDER + 2, w->y + WIN_BORDER + 1,
                   w->title, COLOR_TITLE_TEXT);

  /* Close button [X] */
  fill_rect(w->x + w->width - 10, w->y + WIN_BORDER,
            8, WIN_TITLE_HEIGHT - WIN_BORDER, COLOR_CLOSE_BTN);
  font_draw_char(g_backbuffer, SCREEN_W,
                 w->x + w->width - 9, w->y + WIN_BORDER + 1,
                 'X', COLOR_TITLE_TEXT);

  /* Content area */
  content_x = w->x + WIN_BORDER;
  content_y = w->y + WIN_TITLE_HEIGHT;
  content_w = w->width - WIN_BORDER * 2;
  content_h = w->height - WIN_TITLE_HEIGHT - WIN_BORDER;
  fill_rect(content_x, content_y, content_w, content_h, COLOR_CONTENT_BG);

  /* Check if session is in graphics mode */
  os_session_get_gfx_mode(w->session_id, &gfx_mode);

  if (gfx_mode == 1) {
    /* Graphics mode: blit virtual framebuffer into window content area */
    /* Read a region from the session's VFB that fits the content area */
    unsigned char vfb_line[320]; /* max width we'd ever read */
    int vfb_w = content_w;
    int vfb_h = content_h;
    if (vfb_w > 320) vfb_w = 320;
    if (vfb_h > 200) vfb_h = 200;

    for (row = 0; row < vfb_h; row++) {
      int dst_y = content_y + row;
      if (dst_y < 0 || dst_y >= SCREEN_H) continue;
      if (os_session_read_framebuffer(w->session_id, vfb_line,
                                      0, (unsigned int)row,
                                      (unsigned int)vfb_w, 1) == OS_STATUS_OK) {
        int col;
        for (col = 0; col < vfb_w; col++) {
          int dst_x = content_x + col;
          if (dst_x >= 0 && dst_x < SCREEN_W) {
            g_backbuffer[dst_y * SCREEN_W + dst_x] = vfb_line[col];
          }
        }
      }
    }
  } else {
    /* Text mode: render terminal text using bitmap font */
    for (row = 0; row < WIN_CONTENT_ROWS; row++) {
      if (w->text[row][0] != '\0') {
        font_draw_string(g_backbuffer, SCREEN_W,
                         content_x + 2,
                         content_y + row * (FONT_CHAR_H + 1) + 1,
                         w->text[row], COLOR_CONTENT_FG);
      }
    }

    /* Blinking cursor indicator */
    if (w->focused) {
      int cx = content_x + 2 + w->cursor_col * (FONT_CHAR_W + 1);
      int cy = content_y + w->cursor_row * (FONT_CHAR_H + 1) + 1;
      fill_rect(cx, cy + FONT_CHAR_H, FONT_CHAR_W, 1, COLOR_CONTENT_FG);
    }
  }
}

static void draw_cursor(int mx, int my) {
  int i;
  /* Simple crosshair cursor */
  for (i = -3; i <= 3; i++) {
    int px = mx + i;
    int py = my;
    if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
      g_backbuffer[py * SCREEN_W + px] = COLOR_CURSOR;
    }
    px = mx;
    py = my + i;
    if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
      g_backbuffer[py * SCREEN_W + px] = COLOR_CURSOR;
    }
  }
}

/* Simple bubble sort for z-order rendering */
static void sort_windows_by_z(win_window_t *order[], int count) {
  int i, j;
  for (i = 0; i < count - 1; i++) {
    for (j = 0; j < count - i - 1; j++) {
      if (order[j]->z_order > order[j + 1]->z_order) {
        win_window_t *tmp = order[j];
        order[j] = order[j + 1];
        order[j + 1] = tmp;
      }
    }
  }
}

void compositor_init(void) {
  int i;
  for (i = 0; i < SCREEN_W * SCREEN_H; i++) {
    g_backbuffer[i] = COLOR_DESKTOP;
  }
}

void compositor_render(int mouse_x, int mouse_y) {
  win_window_t *table = win_get_table();
  win_window_t *sorted[WIN_MAX_WINDOWS];
  int count = 0;
  int i;

  /* Fill desktop background */
  for (i = 0; i < SCREEN_W * SCREEN_H; i++) {
    g_backbuffer[i] = COLOR_DESKTOP;
  }

  /* Gather active windows */
  for (i = 0; i < WIN_MAX_WINDOWS; i++) {
    if (table[i].active) {
      sorted[count++] = &table[i];
    }
  }

  /* Sort by z-order (lowest first = drawn first = behind) */
  if (count > 1) {
    sort_windows_by_z(sorted, count);
  }

  /* Draw windows back-to-front */
  for (i = 0; i < count; i++) {
    draw_window(sorted[i]);
  }

  /* Draw cursor on top */
  draw_cursor(mouse_x, mouse_y);

  /* Flush to physical framebuffer */
  os_video_blit(0, 0, SCREEN_W, SCREEN_H, g_backbuffer);
}
