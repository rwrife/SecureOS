/**
 * @file vga_text.c
 * @brief VGA text-mode backend driver.
 *
 * Purpose:
 *   Implements classic VGA text output by writing to the 0xB8000 text buffer.
 *   Provides clear and string rendering primitives registered through video
 *   HAL.
 *
 * Interactions:
 *   - hal/video_hal.c: backend registration and dispatch target.
 *   - core/console.c: writes shell output via video HAL.
 *
 * Launched by:
 *   vga_text_init_primary() is called from kmain during boot.
 *   Not a standalone process; compiled into kernel image.
 */

#include "vga_text.h"

#include "../../hal/video_hal.h"

#define VGA_TEXT_BUFFER ((volatile unsigned short *)0xB8000)
#define VGA_TEXT_WIDTH 80
#define VGA_TEXT_HEIGHT 25
#define VGA_TEXT_ATTR 0x07

static int g_row;
static int g_col;

static inline unsigned short vga_text_entry(char value) {
  return (unsigned short)(VGA_TEXT_ATTR << 8) | (unsigned char)value;
}

static int vga_text_init(void) {
  g_row = 0;
  g_col = 0;
  return 1;
}

static void vga_text_clear(void) {
  int y = 0;
  int x = 0;

  for (y = 0; y < VGA_TEXT_HEIGHT; ++y) {
    for (x = 0; x < VGA_TEXT_WIDTH; ++x) {
      VGA_TEXT_BUFFER[y * VGA_TEXT_WIDTH + x] = vga_text_entry(' ');
    }
  }

  g_row = 0;
  g_col = 0;
}

static void vga_text_putc(char value) {
  if (value == '\n') {
    g_col = 0;
    ++g_row;
  } else {
    VGA_TEXT_BUFFER[g_row * VGA_TEXT_WIDTH + g_col] = vga_text_entry(value);
    ++g_col;
    if (g_col >= VGA_TEXT_WIDTH) {
      g_col = 0;
      ++g_row;
    }
  }

  if (g_row >= VGA_TEXT_HEIGHT) {
    g_row = 0;
  }
}

static void vga_text_write(const char *message) {
  if (message == 0) {
    return;
  }

  while (*message != '\0') {
    vga_text_putc(*message++);
  }
}

static const video_device_t g_vga_text_device = {
  VIDEO_BACKEND_VGA_TEXT,
  "vga-text",
  vga_text_init,
  vga_text_clear,
  vga_text_write,
};

int vga_text_init_primary(void) {
  video_hal_register_primary(&g_vga_text_device);
  return video_hal_init();
}
