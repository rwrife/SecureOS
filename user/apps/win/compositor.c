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
#include "auth_dialog.h"

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
  int row;
  int x_end = x + w;
  int y_end = y + h;

  /* Clip rectangle to screen once instead of testing per pixel. */
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x_end > SCREEN_W) x_end = SCREEN_W;
  if (y_end > SCREEN_H) y_end = SCREEN_H;
  if (x >= x_end || y >= y_end) return;

  for (row = y; row < y_end; row++) {
    unsigned char *dst = &g_backbuffer[row * SCREEN_W + x];
    int span = x_end - x;
    int i;
    for (i = 0; i < span; i++) {
      dst[i] = color;
    }
  }
}

static void draw_window(win_window_t *w) {
  int content_x, content_y;
  int content_w, content_h;
  int row;
  unsigned char tb_color;

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

  /* Read the entire session VFB in a single bulk call instead of row-by-row.
   * This reduces ~160 bridge/syscall round-trips per window per frame down to
   * one, dramatically improving frame rate and therefore mouse responsiveness
   * (see plans/2026-05-28-wm-mouse-performance.md). */
  {
    static unsigned char vfb_bulk[320 * 200];
    int vfb_w = content_w;
    int vfb_h = content_h;
    if (vfb_w > 320) vfb_w = 320;
    if (vfb_h > 200) vfb_h = 200;

    if (os_session_read_framebuffer(w->session_id, vfb_bulk,
                                    0, 0,
                                    (unsigned int)vfb_w,
                                    (unsigned int)vfb_h) == OS_STATUS_OK) {
      /* Per-row clipped copy. The previous per-pixel bounds-checked inner
       * loop was a major drag on frame rate (~40K iterations per window per
       * frame at -O0) and contributed to the laggy mouse — see
       * plans/2026-05-29-wm-render-speedup.md. */
      int src_x_off = 0;
      int src_y_off = 0;
      int dx = content_x;
      int dy = content_y;
      int cw = vfb_w;
      int ch = vfb_h;

      if (dx < 0) { src_x_off = -dx; cw += dx; dx = 0; }
      if (dy < 0) { src_y_off = -dy; ch += dy; dy = 0; }
      if (dx + cw > SCREEN_W) cw = SCREEN_W - dx;
      if (dy + ch > SCREEN_H) ch = SCREEN_H - dy;
      if (cw > 0 && ch > 0) {
        for (row = 0; row < ch; row++) {
          unsigned char *dst = &g_backbuffer[(dy + row) * SCREEN_W + dx];
          const unsigned char *src =
              &vfb_bulk[(src_y_off + row) * vfb_w + src_x_off];
          int col;
          for (col = 0; col < cw; col++) {
            dst[col] = src[col];
          }
        }
      }
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

  /* Draw auth dialog overlay if active */
  auth_dialog_render(g_backbuffer, SCREEN_W, SCREEN_H);

  /* Draw Quit button in top-right corner */
  fill_rect(296, 0, 24, 10, COLOR_CLOSE_BTN);
  font_draw_string(g_backbuffer, SCREEN_W, 298, 2, "Quit", COLOR_TITLE_TEXT);

  /* Draw cursor on top */
  draw_cursor(mouse_x, mouse_y);

  /* Flush to physical framebuffer */
  os_video_blit(0, 0, SCREEN_W, SCREEN_H, g_backbuffer);
}
