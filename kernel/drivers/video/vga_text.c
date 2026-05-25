/**
 * @file vga_text.c
 * @brief VGA text-mode backend driver.
 *
 * Purpose:
 *   Implements classic VGA text output by writing to the 0xB8000 text buffer.
 *   Provides clear, string rendering, and colored string rendering primitives
 *   registered through video HAL.
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

#include <stdint.h>

#include "../../hal/video_hal.h"

#define VGA_TEXT_BUFFER ((volatile unsigned short *)0xB8000)
#define VGA_TEXT_WIDTH 80
#define VGA_TEXT_HEIGHT 25
#define VGA_TEXT_ATTR 0x07

/* VGA CRT controller ports for hardware cursor */
#define VGA_CRTC_INDEX 0x3D4u
#define VGA_CRTC_DATA  0x3D5u
#define VGA_CURSOR_HIGH 0x0Eu
#define VGA_CURSOR_LOW  0x0Fu

static int g_row;
static int g_col;

static inline void vga_crtc_outb(unsigned short port, unsigned char value) {
  __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void vga_text_update_cursor(void) {
  unsigned short pos = (unsigned short)(g_row * VGA_TEXT_WIDTH + g_col);
  vga_crtc_outb(VGA_CRTC_INDEX, VGA_CURSOR_HIGH);
  vga_crtc_outb(VGA_CRTC_DATA, (unsigned char)(pos >> 8));
  vga_crtc_outb(VGA_CRTC_INDEX, VGA_CURSOR_LOW);
  vga_crtc_outb(VGA_CRTC_DATA, (unsigned char)(pos & 0xFF));
}

static inline unsigned short vga_text_entry(char value) {
  return (unsigned short)(VGA_TEXT_ATTR << 8) | (unsigned char)value;
}

static inline unsigned short vga_text_entry_color(char value, uint8_t attr) {
  return (unsigned short)((unsigned short)attr << 8) | (unsigned char)value;
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
  vga_text_update_cursor();
}

static void vga_text_scroll(void) {
  int y, x;
  for (y = 0; y < VGA_TEXT_HEIGHT - 1; ++y) {
    for (x = 0; x < VGA_TEXT_WIDTH; ++x) {
      VGA_TEXT_BUFFER[y * VGA_TEXT_WIDTH + x] =
          VGA_TEXT_BUFFER[(y + 1) * VGA_TEXT_WIDTH + x];
    }
  }
  /* Clear the last row */
  for (x = 0; x < VGA_TEXT_WIDTH; ++x) {
    VGA_TEXT_BUFFER[(VGA_TEXT_HEIGHT - 1) * VGA_TEXT_WIDTH + x] =
        vga_text_entry(' ');
  }
  g_row = VGA_TEXT_HEIGHT - 1;
}

static void vga_text_advance_row(void) {
  g_col = 0;
  ++g_row;
  if (g_row >= VGA_TEXT_HEIGHT) {
    vga_text_scroll();
  }
}

static void vga_text_putc(char value) {
  if (value == '\n') {
    vga_text_advance_row();
  } else if (value == '\b') {
    if (g_col > 0) {
      --g_col;
      VGA_TEXT_BUFFER[g_row * VGA_TEXT_WIDTH + g_col] = vga_text_entry(' ');
    }
  } else {
    VGA_TEXT_BUFFER[g_row * VGA_TEXT_WIDTH + g_col] = vga_text_entry(value);
    ++g_col;
    if (g_col >= VGA_TEXT_WIDTH) {
      vga_text_advance_row();
    }
  }
  vga_text_update_cursor();
}

static void vga_text_putc_color(char value, uint8_t attr) {
  if (value == '\n') {
    vga_text_advance_row();
  } else if (value == '\b') {
    if (g_col > 0) {
      --g_col;
      VGA_TEXT_BUFFER[g_row * VGA_TEXT_WIDTH + g_col] = vga_text_entry(' ');
    }
  } else {
    VGA_TEXT_BUFFER[g_row * VGA_TEXT_WIDTH + g_col] =
        vga_text_entry_color(value, attr);
    ++g_col;
    if (g_col >= VGA_TEXT_WIDTH) {
      vga_text_advance_row();
    }
  }
  vga_text_update_cursor();
}

static void vga_text_write(const char *message) {
  if (message == 0) {
    return;
  }

  while (*message != '\0') {
    vga_text_putc(*message++);
  }
}

static void vga_text_write_color(const char *message, uint8_t attr) {
  if (message == 0) {
    return;
  }

  while (*message != '\0') {
    vga_text_putc_color(*message++, attr);
  }
}

static const video_device_t g_vga_text_device = {
  VIDEO_BACKEND_VGA_TEXT,
  "vga-text",
  vga_text_init,
  vga_text_clear,
  vga_text_write,
  vga_text_write_color,
};

int vga_text_init_primary(void) {
  video_hal_register_primary(&g_vga_text_device);
  return video_hal_init();
}
