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
#include "auth_dialog.h"

/* Declared in input.c */
int input_get_mouse_x(void);
int input_get_mouse_y(void);

int main(void) {
  win_window_t *initial_win;
  unsigned int session_id = 0;

  os_console_write("[win] main() entered\n");

  /* Enter graphics mode */
  os_console_write("[win] calling os_video_set_mode(GFX)\n");
  {
    os_status_t st = os_video_set_mode(OS_VIDEO_MODE_GFX);
    if (st != OS_STATUS_OK) {
      os_console_write("[win] FAILED to enter gfx mode\n");
      return 1;
    }
  }
  os_console_write("[win] gfx mode OK\n");

  /* Initialize subsystems */
  os_console_write("[win] init subsystems\n");
  win_init();
  os_console_write("[win] win_init done\n");
  compositor_init();
  os_console_write("[win] compositor_init done\n");
  input_init();
  os_console_write("[win] input_init done\n");

  /* Create a session for the first window */
  os_console_write("[win] creating session\n");
  if (os_session_create(&session_id) != OS_STATUS_OK) {
    /* Fallback: use session 1 if create fails */
    session_id = 1;
    os_console_write("[win] session_create failed, using 1\n");
  } else {
    os_console_write("[win] session created OK\n");
  }

  /* Mark the session as WM-managed so video calls go to virtual framebuffer
   * and auth prompts route through the event bus.
   * Set VFB size to fit inside the window (screen minus chrome). */
  os_console_write("[win] setting wm_managed\n");
  {
    /* Max content area that fits on 320x200 screen with window chrome */
    unsigned int vfb_w = 320 - WIN_BORDER * 2;       /* 318 */
    unsigned int vfb_h = 200 - WIN_TITLE_HEIGHT - WIN_BORDER * 2; /* 188 */
    os_session_set_vfb_size(session_id, vfb_w, vfb_h);
  }
  os_session_set_wm_managed(session_id, 1);
  os_console_write("[win] wm_managed set\n");

  /* Create initial window — positioned at origin to fill the screen */
  os_console_write("[win] creating window\n");
  initial_win = win_create(0, 0, "Session 1", session_id);
  if (initial_win != 0) {
    win_set_focus(initial_win);
    os_console_write("[win] window created & focused\n");
  } else {
    os_console_write("[win] win_create returned NULL\n");
  }

  os_console_write("[win] entering main loop\n");

  /* Main event loop */
  while (1) {
    /* Poll for auth prompts — must happen before input so dialog can intercept */
    auth_dialog_poll();

    /* Process input; returns 1 if ESC pressed */
    if (input_update()) {
      break;
    }

    /* Tick all active sessions to process injected input */
    {
      win_window_t *table = win_get_table();
      int i;
      for (i = 0; i < WIN_MAX_WINDOWS; i++) {
        if (table[i].active) {
          os_session_tick(table[i].session_id);
        }
      }
    }

    /* Render frame — compositor reads VFB pixels directly */
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
  os_console_write("[win] exited\n");
  return 0;
}
