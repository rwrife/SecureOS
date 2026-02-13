#include "vga.h"

#define VGA_BUFFER ((volatile unsigned short *)0xB8000)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ATTR 0x07

static int row = 0;
static int col = 0;

static inline unsigned short vga_entry(char c) {
  return (unsigned short)VGA_ATTR << 8 | (unsigned char)c;
}

void vga_clear(void) {
  for (int y = 0; y < VGA_HEIGHT; y++) {
    for (int x = 0; x < VGA_WIDTH; x++) {
      VGA_BUFFER[y * VGA_WIDTH + x] = vga_entry(' ');
    }
  }
  row = 0;
  col = 0;
}

static void vga_putc(char c) {
  if (c == '\n') {
    col = 0;
    row++;
  } else {
    VGA_BUFFER[row * VGA_WIDTH + col] = vga_entry(c);
    col++;
    if (col >= VGA_WIDTH) {
      col = 0;
      row++;
    }
  }

  if (row >= VGA_HEIGHT) {
    row = 0;
  }
}

void vga_write(const char *s) {
  for (; *s; s++) {
    vga_putc(*s);
  }
}
