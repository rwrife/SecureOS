/**
 * @file pixels.c
 * @brief Small in-guest graphics sample for the developer image.
 *
 * Edit in /apps/dev with /apps/edit, compile with cc once DEMO-03/04 land,
 * then launch through the ordinary shell after the unsigned-run prompt.
 * Uses only the public SecureOS syscall header; the video and input calls
 * are served by the native bridge through the kernel HAL.
 */
#include "secureos_api.h"

int main(void) {
  char key = 0;
  int x = 0, y = 0;
  unsigned char buttons = 0;

  if (os_video_set_mode(OS_VIDEO_MODE_GFX) != OS_STATUS_OK) {
    os_console_write("pixels: video unavailable\n");
    return 1;
  }
  os_video_clear();
  os_video_draw_rect(16, 16, 64, 40, 4);
  os_mouse_enable();
  /* Press Escape to leave; click to draw. */
  for (;;) {
    if (os_input_read_char(&key) == OS_STATUS_OK && key == 27)
      break;
    if (os_mouse_get_state(&x, &y, &buttons) == OS_STATUS_OK && (buttons & 1))
      os_video_put_pixel(x, y, 15);
  }
  os_mouse_disable();
  os_video_set_mode(OS_VIDEO_MODE_TEXT);
  return 0;
}
