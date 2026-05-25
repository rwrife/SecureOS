/**
 * @file main.c
 * @brief "draw" – interactive mouse-driven pixel art application.
 *
 * Purpose:
 *   Switches to VGA mode 13h (320x200, 256 colors) and presents a drawing
 *   canvas. The user draws by holding the left mouse button and moving the
 *   cursor. Pressing ESC exits the application and restores text mode.
 *
 * Interactions:
 *   - secureos_api.h: uses os_video_set_mode, os_video_put_pixel,
 *     os_video_clear, os_video_draw_rect, os_mouse_get_state,
 *     os_input_read_char syscalls.
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

/* Mouse cursor size */
#define CURSOR_SIZE 3

static unsigned char current_color = COLOR_WHITE;
static int brush_size = 2;

static void draw_palette_bar(void) {
  int i;
  int bar_y = 190;
  int swatch_w = 320 / PALETTE_SIZE;

  for (i = 0; i < PALETTE_SIZE; i++) {
    os_video_draw_rect(i * swatch_w, bar_y, swatch_w, 10, palette[i]);
  }
}

static void draw_cursor(int x, int y, unsigned char color) {
  /* Simple crosshair cursor */
  int i;
  for (i = -CURSOR_SIZE; i <= CURSOR_SIZE; i++) {
    os_video_put_pixel(x + i, y, color);
    os_video_put_pixel(x, y + i, color);
  }
}

int main(void) {
  int running = 1;
  int prev_mx = -1;
  int prev_my = -1;
  int palette_idx = 0;

  /* Enter graphics mode */
  if (os_video_set_mode(OS_VIDEO_MODE_GFX) != OS_STATUS_OK) {
    os_console_write("draw: failed to enter graphics mode\n");
    return 1;
  }

  /* Clear to black and draw palette */
  os_video_clear();
  draw_palette_bar();

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
      /* 'c' to clear canvas */
      if (key == 'c' || key == 'C') {
        os_video_clear();
        draw_palette_bar();
      }
      /* Number keys 1-8 select palette color */
      if (key >= '1' && key <= '8') {
        palette_idx = key - '1';
        current_color = palette[palette_idx];
      }
      /* +/- to change brush size */
      if (key == '+' || key == '=') {
        if (brush_size < 10) brush_size++;
      }
      if (key == '-') {
        if (brush_size > 1) brush_size--;
      }
    }

    /* Get mouse state */
    os_mouse_get_state(&mx, &my, &buttons);

    /* Scale mouse from 80x25 text grid to 320x200 pixel grid */
    mx = mx * 4;
    my = my * 8;

    /* Clamp to canvas (above palette bar) */
    if (mx >= 320) mx = 319;
    if (my >= 190) my = 189;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;

    /* Draw if left button held */
    if (buttons & 0x01u) {
      os_video_draw_rect(mx - brush_size / 2, my - brush_size / 2,
                         brush_size, brush_size, current_color);
    }

    /* Right click picks color from palette bar */
    if (buttons & 0x02u) {
      int raw_my = 0;
      os_mouse_get_state(&mx, &raw_my, &buttons);
      raw_my = raw_my * 8;
      if (raw_my >= 190) {
        int raw_mx = mx * 4;
        int swatch_w = 320 / PALETTE_SIZE;
        int idx = raw_mx / swatch_w;
        if (idx >= 0 && idx < PALETTE_SIZE) {
          current_color = palette[idx];
        }
      }
    }

    /* Draw cursor at current position */
    draw_cursor(mx, my, current_color);

    prev_mx = mx;
    prev_my = my;
  }

  (void)prev_mx;
  (void)prev_my;

  /* Restore text mode (also done automatically by kernel on exit) */
  os_video_set_mode(OS_VIDEO_MODE_TEXT);
  os_console_write("draw: exited\n");
  return 0;
}
