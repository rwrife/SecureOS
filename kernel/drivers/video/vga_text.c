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

/* --- Mouse cursor overlay ---
 * The mouse cursor is rendered by inverting the attribute byte at the
 * cursor's text-mode cell position. We save the original cell value
 * so it can be restored when the cursor moves.
 */

static int g_mouse_cursor_active;
static int g_mouse_cursor_col;
static int g_mouse_cursor_row;
static unsigned short g_mouse_saved_cell;

void vga_text_mouse_cursor_show(int col, int row) {
  if (col < 0 || col >= VGA_TEXT_WIDTH || row < 0 || row >= VGA_TEXT_HEIGHT) {
    return;
  }

  /* If cursor was already shown elsewhere, restore old cell first */
  if (g_mouse_cursor_active) {
    VGA_TEXT_BUFFER[g_mouse_cursor_row * VGA_TEXT_WIDTH + g_mouse_cursor_col] =
        g_mouse_saved_cell;
  }

  g_mouse_cursor_col = col;
  g_mouse_cursor_row = row;
  g_mouse_cursor_active = 1;

  /* Save the cell under the new position */
  int offset = row * VGA_TEXT_WIDTH + col;
  g_mouse_saved_cell = VGA_TEXT_BUFFER[offset];

  /* Render cursor by inverting foreground/background colors */
  unsigned short cell = g_mouse_saved_cell;
  unsigned char attr = (unsigned char)(cell >> 8);
  unsigned char fg = attr & 0x0Fu;
  unsigned char bg = (attr >> 4) & 0x0Fu;
  unsigned char inverted = (unsigned char)((fg << 4) | bg);
  VGA_TEXT_BUFFER[offset] = (unsigned short)((unsigned short)inverted << 8) |
                            (cell & 0x00FFu);
}

void vga_text_mouse_cursor_hide(void) {
  if (!g_mouse_cursor_active) {
    return;
  }

  VGA_TEXT_BUFFER[g_mouse_cursor_row * VGA_TEXT_WIDTH + g_mouse_cursor_col] =
      g_mouse_saved_cell;
  g_mouse_cursor_active = 0;
}

void vga_text_set_cursor(int col, int row) {
  if (col < 0 || col >= VGA_TEXT_WIDTH || row < 0 || row >= VGA_TEXT_HEIGHT) {
    return;
  }
  g_col = col;
  g_row = row;
  vga_text_update_cursor();
}

void vga_text_putchar_at(int col, int row, char ch, unsigned char attr) {
  if (col < 0 || col >= VGA_TEXT_WIDTH || row < 0 || row >= VGA_TEXT_HEIGHT) {
    return;
  }
  int offset = row * VGA_TEXT_WIDTH + col;
  VGA_TEXT_BUFFER[offset] = vga_text_entry_color(ch, attr);
}

