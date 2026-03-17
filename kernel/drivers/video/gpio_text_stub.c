/**
 * @file gpio_text_stub.c
 * @brief Stub GPIO-style text backend for video HAL.
 *
 * Purpose:
 *   Provides a minimal text sink backend that simulates GPIO-driven display
 *   output by buffering recent text bytes. This is a placeholder for future
 *   bit-banged or peripheral GPIO display implementations.
 *
 * Interactions:
 *   - hal/video_hal.c: backend registration and dispatch target.
 *   - core/kmain.c: optional final fallback backend.
 *
 * Launched by:
 *   gpio_text_stub_init_primary() is called during kernel startup.
 *   Not a standalone process; compiled into kernel image.
 */

#include "gpio_text_stub.h"

#include "../../hal/video_hal.h"

#define GPIO_TEXT_STUB_BUFFER_SIZE 1024

static char g_gpio_text_buffer[GPIO_TEXT_STUB_BUFFER_SIZE];
static int g_gpio_text_cursor;

static int gpio_text_stub_init(void) {
  g_gpio_text_cursor = 0;
  return 1;
}

static void gpio_text_stub_clear(void) {
  int i = 0;
  for (i = 0; i < GPIO_TEXT_STUB_BUFFER_SIZE; ++i) {
    g_gpio_text_buffer[i] = '\0';
  }
  g_gpio_text_cursor = 0;
}

static void gpio_text_stub_write(const char *message) {
  if (message == 0) {
    return;
  }

  while (*message != '\0') {
    g_gpio_text_buffer[g_gpio_text_cursor++] = *message++;
    if (g_gpio_text_cursor >= GPIO_TEXT_STUB_BUFFER_SIZE - 1) {
      g_gpio_text_cursor = 0;
    }
  }

  g_gpio_text_buffer[g_gpio_text_cursor] = '\0';
}

static const video_device_t g_gpio_text_stub_device = {
  VIDEO_BACKEND_GPIO_TEXT_STUB,
  "gpio-text-stub",
  gpio_text_stub_init,
  gpio_text_stub_clear,
  gpio_text_stub_write,
};

int gpio_text_stub_init_primary(void) {
  video_hal_register_primary(&g_gpio_text_stub_device);
  return video_hal_init();
}
