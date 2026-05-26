/**
 * @file main.c
 * @brief "draw" – interactive mouse-driven pixel art application.
 *
 * Purpose:
 *   Switches to VGA mode 13h (320x200, 256 colors) and presents a drawing
 *   canvas. The user draws by holding the left mouse button and moving the
 *   cursor. Pressing ESC exits the application and restores text mode.
 *
 *   The system cursor is rendered by the window manager — the app does not
 *   manage its own cursor. It receives mouse coordinates relative to its
 *   content area via os_mouse_get_state.
 *
 * Interactions:
 *   - secureos_api.h: uses os_video_set_mode, os_video_put_pixel,
 *     os_video_get_pixel, os_video_clear, os_video_draw_rect,
 *     os_mouse_get_state, os_input_read_char syscalls.
 *   - kernel/drivers/video/vga_gfx: provides mode 13h pixel drawing.
 *   - kernel/hal/mouse_hal: provides mouse position data via syscalls.
 *   - kernel/hal/input_hal: provides keyboard input via syscalls.
 *
 * Launched by:
 *   Invoked as a user-space application via "run /apps/draw.bin".
 *   Built as a standalone ELF binary and wrapped as SOF binary.
 */

#include "secureos_api.h"

#define KEY_ESCAPE 0x1Bu

/* Default VGA palette color indices */
#define COLOR_BLACK   0
#define COLOR_WHITE   15
#define COLOR_RED     4
#define COLOR_GREEN   2
#define COLOR_BLUE    1
#define COLOR_YELLOW  14
#define COLOR_CYAN    3
#define COLOR_MAGENTA 5
#define COLOR_GREY    7

/* Palette for user selection */
static const unsigned char palette[] = {
  COLOR_WHITE, COLOR_RED, COLOR_GREEN, COLOR_BLUE,
  COLOR_YELLOW, COLOR_CYAN, COLOR_MAGENTA, COLOR_GREY
};
#define PALETTE_SIZE 8

static unsigned char current_color = COLOR_WHITE;
static int brush_size = 2;
static int screen_w = 320;
static int screen_h = 200;

static void draw_palette_bar(void) {
  int i;
  int bar_h = 10;
  int bar_y = screen_h - bar_h;
  int swatch_w = screen_w / PALETTE_SIZE;

  for (i = 0; i < PALETTE_SIZE; i++) {
    os_video_draw_rect(i * swatch_w, bar_y, swatch_w, bar_h, palette[i]);
  }
}

int main(void) {
  int running = 1;

  /* Enter graphics mode */
  if (os_video_set_mode(OS_VIDEO_MODE_GFX) != OS_STATUS_OK) {
    os_console_write("draw: failed to enter graphics mode\n");
    return 1;
  }

  /* Query actual resolution (may differ from 320x200 in WM mode) */
  os_video_get_resolution(&screen_w, &screen_h);

  /* Clear to black and draw palette */
  os_video_clear();
  draw_palette_bar();

  /* Enable system-managed cursor */
  os_mouse_enable();

  while (running) {
    char key = '\0';
    int mx = 0, my = 0;
    unsigned char buttons = 0;

    /* Check keyboard */
    if (os_input_read_char(&key) == OS_STATUS_OK) {
      if (key == KEY_ESCAPE) {
        running = 0;
        break;
      }
      if (key == 'c' || key == 'C') {
        os_video_clear();
        draw_palette_bar();
      }
      /* Number keys 1-8 select palette color */
      if (key >= '1' && key <= '8') {
        current_color = palette[key - '1'];
      }
      /* +/- to change brush size */
      if (key == '+' || key == '=') {
        if (brush_size < 10) brush_size++;
      }
      if (key == '-') {
        if (brush_size > 1) brush_size--;
      }
    }

    /* Get mouse state (coordinates relative to content area) */
    os_mouse_get_state(&mx, &my, &buttons);

    /* Clamp to canvas (above palette bar) */
    if (mx >= screen_w) mx = screen_w - 1;
    if (my >= screen_h - 10) my = screen_h - 11;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;

    /* Draw with left button */
    if (buttons & 0x01u) {
      os_video_draw_rect(mx - brush_size / 2, my - brush_size / 2,
                         brush_size, brush_size, current_color);
    }

    /* Right click picks color from palette bar */
    if (buttons & 0x02u) {
      int raw_mx = 0, raw_my = 0;
      unsigned char dummy = 0;
      os_mouse_get_state(&raw_mx, &raw_my, &dummy);
      if (raw_my >= screen_h - 10) {
        int swatch_w = screen_w / PALETTE_SIZE;
        int idx = raw_mx / swatch_w;
        if (idx >= 0 && idx < PALETTE_SIZE) {
          current_color = palette[idx];
        }
      }
    }
  }

  /* Disable cursor and restore text mode */
  os_mouse_disable();
  os_video_set_mode(OS_VIDEO_MODE_TEXT);
  os_console_write("draw: exited\n");
  return 0;
}
