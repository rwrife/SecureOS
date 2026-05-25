/**
 * @file main.c
 * @brief "draw" – interactive mouse-driven pixel art application.
 *
 * Purpose:
 *   Clears the VGA text screen and presents a drawing canvas. The user
 *   draws by holding the left mouse button and moving the cursor. Each
 *   "pixel" is a block character (U+2588, rendered as '#') placed at the
 *   current mouse cell position. Pressing ESC exits the application and
 *   returns to the shell.
 *
 *   The bottom row displays a minimal status bar with instructions.
 *
 * Interactions:
 *   - secureos_api.h: uses os_video_clear, os_video_putchar_at,
 *     os_mouse_get_state, os_input_read_char syscalls.
 *   - mouselib: optional dependency for enhanced mouse queries.
 *   - kernel/hal/mouse_hal: provides mouse position data via syscalls.
 *   - kernel/hal/input_hal: provides keyboard input via syscalls.
 *
 * Launched by:
 *   Invoked as a user-space application via "run /apps/draw.bin".
 *   Built as a standalone ELF binary and wrapped as SOF binary.
 */

#include "secureos_api.h"

#define SCREEN_WIDTH  80
#define SCREEN_HEIGHT 25
#define CANVAS_HEIGHT 24  /* Reserve bottom row for status bar */

#define KEY_ESCAPE 0x1Bu

/* Color attributes for VGA text mode */
#define ATTR_CANVAS   0x00u  /* Black on black (empty) */
#define ATTR_PIXEL    0x0Fu  /* White on black (drawn pixel) */
#define ATTR_STATUS   0x70u  /* Black on grey (status bar) */
#define ATTR_CURSOR   0x0Au  /* Green on black (cursor when not drawing) */

/* Drawing character */
#define PIXEL_CHAR    '#'
#define CURSOR_CHAR   '+'

static void draw_status_bar(void) {
  const char *msg = " DRAW: Left-click to paint | ESC to quit ";
  int i;

  /* Fill status bar background */
  for (i = 0; i < SCREEN_WIDTH; i++) {
    os_video_putchar_at(i, CANVAS_HEIGHT, ' ', ATTR_STATUS);
  }

  /* Write status text */
  for (i = 0; msg[i] != '\0' && i < SCREEN_WIDTH; i++) {
    os_video_putchar_at(i, CANVAS_HEIGHT, msg[i], ATTR_STATUS);
  }
}

static void clear_canvas(void) {
  int x, y;
  for (y = 0; y < CANVAS_HEIGHT; y++) {
    for (x = 0; x < SCREEN_WIDTH; x++) {
      os_video_putchar_at(x, y, ' ', ATTR_CANVAS);
    }
  }
}

int main(void) {
  int prev_x = -1;
  int prev_y = -1;
  int running = 1;

  /* Clear screen and set up UI */
  os_video_clear();
  clear_canvas();
  draw_status_bar();

  while (running) {
    char key = '\0';
    int mouse_x = 0;
    int mouse_y = 0;
    unsigned char buttons = 0;

    /* Check for keyboard input (ESC to quit) */
    if (os_input_read_char(&key) == OS_STATUS_OK) {
      if (key == KEY_ESCAPE) {
        running = 0;
        break;
      }
      /* 'c' to clear canvas */
      if (key == 'c' || key == 'C') {
        clear_canvas();
        draw_status_bar();
        prev_x = -1;
        prev_y = -1;
      }
    }

    /* Get mouse state */
    os_mouse_get_state(&mouse_x, &mouse_y, &buttons);

    /* Clamp to canvas area */
    if (mouse_y >= CANVAS_HEIGHT) {
      mouse_y = CANVAS_HEIGHT - 1;
    }

    /* Draw pixel if left button is held */
    if (buttons & 0x01u) {
      os_video_putchar_at(mouse_x, mouse_y, PIXEL_CHAR, ATTR_PIXEL);
    } else {
      /* Show cursor position indicator (if not over a drawn pixel) */
      if (prev_x >= 0 && prev_y >= 0 &&
          prev_x != mouse_x && prev_y != mouse_y) {
        /* We don't erase previous cursor - it might be a drawn pixel.
         * The cursor is transient and only shown via the mouse overlay. */
      }
    }

    prev_x = mouse_x;
    prev_y = mouse_y;
  }

  /* Restore screen */
  os_video_clear();
  os_console_write("draw: exited\n");
  return 0;
}
