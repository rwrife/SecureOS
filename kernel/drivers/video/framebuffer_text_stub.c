/**
 * @file framebuffer_text_stub.c
 * @brief Stub framebuffer-style text backend for video HAL.
 *
 * Purpose:
 *   Provides a non-hardware framebuffer text fallback backend that keeps
 *   text output in a shadow character grid. This allows validating backend
 *   selection flow before a real pixel framebuffer driver is implemented.
 *
 * Interactions:
 *   - hal/video_hal.c: backend registration and dispatch target.
 *   - core/kmain.c: fallback backend option when VGA text is not selected.
 *
 * Launched by:
 *   framebuffer_text_stub_init_primary() is called during kernel startup.
 *   Not a standalone process; compiled into kernel image.
 */

#include "framebuffer_text_stub.h"

#include "../../hal/video_hal.h"

#define FB_STUB_WIDTH 80
#define FB_STUB_HEIGHT 25
#define FB_STUB_CELLS (FB_STUB_WIDTH * FB_STUB_HEIGHT)

static char g_fb_stub_cells[FB_STUB_CELLS];
static int g_fb_stub_row;
static int g_fb_stub_col;

static int framebuffer_text_stub_init(void) {
  g_fb_stub_row = 0;
  g_fb_stub_col = 0;
  return 1;
}

static void framebuffer_text_stub_clear(void) {
  int i = 0;
  for (i = 0; i < FB_STUB_CELLS; ++i) {
    g_fb_stub_cells[i] = ' ';
  }
  g_fb_stub_row = 0;
  g_fb_stub_col = 0;
}

static void framebuffer_text_stub_putc(char value) {
  if (value == '\n') {
    g_fb_stub_col = 0;
    ++g_fb_stub_row;
  } else {
    g_fb_stub_cells[g_fb_stub_row * FB_STUB_WIDTH + g_fb_stub_col] = value;
    ++g_fb_stub_col;
    if (g_fb_stub_col >= FB_STUB_WIDTH) {
      g_fb_stub_col = 0;
      ++g_fb_stub_row;
    }
  }

  if (g_fb_stub_row >= FB_STUB_HEIGHT) {
    g_fb_stub_row = 0;
  }
}

static void framebuffer_text_stub_write(const char *message) {
  if (message == 0) {
    return;
  }

  while (*message != '\0') {
    framebuffer_text_stub_putc(*message++);
  }
}

static const video_device_t g_framebuffer_text_stub_device = {
  VIDEO_BACKEND_FRAMEBUFFER_STUB,
  "framebuffer-stub",
  framebuffer_text_stub_init,
  framebuffer_text_stub_clear,
  framebuffer_text_stub_write,
};

int framebuffer_text_stub_init_primary(void) {
  video_hal_register_primary(&g_framebuffer_text_stub_device);
  return video_hal_init();
}
