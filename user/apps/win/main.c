/**
 * @file main.c
 * @brief "win" – SecureOS window manager application.
 *
 * Purpose:
 *   Initializes VGA graphics mode (320x200x256), creates an initial terminal
 *   window bound to a new session, and runs the window manager event loop.
 *   Each frame: polls input (mouse/keyboard), reads session output into
 *   window text buffers, and composites all windows to the screen.
 *
 *   The window manager allows dragging windows by their title bar, clicking
 *   to give focus (for keyboard input routing), and closing windows via the
 *   [X] button. Pressing ESC exits the window manager and restores text mode.
 *
 * Interactions:
 *   - secureos_api.h: all kernel syscalls (video, mouse, input, session).
 *   - window.h: window state management.
 *   - compositor.h: screen rendering.
 *   - input.h: input dispatch (drag, focus, keyboard routing).
 *   - font.h: bitmap font for text rendering.
 *
 * Launched by:
 *   Invoked as a user-space application via "run /apps/win.bin".
 *   Built as a standalone ELF binary and wrapped as SOF binary.
 */

#include "secureos_api.h"
#include "window.h"
#include "compositor.h"
#include "input.h"

/* Declared in input.c */
int input_get_mouse_x(void);
int input_get_mouse_y(void);

int main(void) {
  win_window_t *initial_win;
  unsigned int session_id = 0;

  /* Enter graphics mode */
  if (os_video_set_mode(OS_VIDEO_MODE_GFX) != OS_STATUS_OK) {
    os_console_write("win: failed to enter graphics mode\n");
    return 1;
  }

  /* Initialize subsystems */
  win_init();
  compositor_init();
  input_init();

  /* Create a session for the first window */
  if (os_session_create(&session_id) != OS_STATUS_OK) {
    /* Fallback: use session 1 if create fails */
    session_id = 1;
  }

  /* Create initial window */
  initial_win = win_create(10, 10, "Session 1", session_id);
  if (initial_win != 0) {
    win_set_focus(initial_win);
    win_puts(initial_win, "SecureOS Window Manager v1\n");
    win_puts(initial_win, "Type to interact. ESC to exit.\n");
    win_puts(initial_win, "> ");
  }

  /* Main event loop */
  while (1) {
    /* Process input; returns 1 if ESC pressed */
    if (input_update()) {
      break;
    }

    /* Read session output into window text buffers */
    {
      win_window_t *table = win_get_table();
      int i;
      for (i = 0; i < WIN_MAX_WINDOWS; i++) {
        if (table[i].active) {
          char buf[64];
          unsigned int len = 0;
          if (os_session_read_output(table[i].session_id, buf, sizeof(buf) - 1, &len) == OS_STATUS_OK && len > 0) {
            buf[len] = '\0';
            win_puts(&table[i], buf);
          }
        }
      }
    }

    /* Render frame */
    compositor_render(input_get_mouse_x(), input_get_mouse_y());

    /* Check if all windows are closed */
    {
      win_window_t *table = win_get_table();
      int any_active = 0;
      int i;
      for (i = 0; i < WIN_MAX_WINDOWS; i++) {
        if (table[i].active) { any_active = 1; break; }
      }
      if (!any_active) break;
    }
  }

  /* Restore text mode */
  os_video_set_mode(OS_VIDEO_MODE_TEXT);
  os_console_write("win: exited\n");
  return 0;
}
