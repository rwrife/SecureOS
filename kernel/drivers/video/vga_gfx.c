/**
 * @file vga_gfx.c
 * @brief VGA mode 13h (320x200x256) graphics driver implementation.
 *
 * Purpose:
 *   Implements pixel-level drawing using VGA mode 13h. This mode provides
 *   a simple linear framebuffer at 0xA0000 with 320x200 resolution and
 *   256 colors from the default VGA palette. Each byte in the framebuffer
 *   maps directly to one pixel on screen.
 *
 * Interactions:
 *   - kernel/user/launcher_exec.c: the native bridge calls vga_gfx_*
 *     functions when apps invoke os_video_put_pixel, os_video_set_mode, etc.
 *   - kernel/drivers/video/vga_text.c: text mode (mode 03h) is restored
 *     via vga_gfx_leave() when an app exits graphics mode.
 *
 * Launched by:
 *   Called from the syscall bridge in launcher_exec.c; not a standalone
 *   process. Compiled into the kernel image.
 */

#include "vga_gfx.h"
#include "vga_text.h"

#define VGA_GFX_FRAMEBUFFER ((volatile unsigned char *)0xA0000)
#define VGA_GFX_FB_SIZE     (VGA_GFX_WIDTH * VGA_GFX_HEIGHT)

/* VGA font is stored in plane 2. Each character uses 32 bytes (16 rows of
 * glyph data + 16 bytes padding). 256 characters = 8192 bytes total.
 * Mode 13h chain-4 writes corrupt plane 2, so we save/restore the font. */
#define VGA_FONT_CHARS     256
#define VGA_FONT_CHAR_SIZE 32
#define VGA_FONT_SIZE      (VGA_FONT_CHARS * VGA_FONT_CHAR_SIZE)

static int g_gfx_active = 0;
static unsigned char g_font_save[VGA_FONT_SIZE];

static inline void vga_outb(unsigned short port, unsigned char value) {
  __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char vga_inb(unsigned short port) {
  unsigned char ret;
  __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

/**
 * Access plane 2 directly for font save/restore.
 * Sets VGA registers to map plane 2 at 0xA0000 in sequential mode.
 */
static void vga_font_access_begin(void) {
  /* Sequencer: write to plane 2 only, sequential access */
  vga_outb(0x3C4, 0x02); vga_outb(0x3C5, 0x04); /* map mask = plane 2 */
  vga_outb(0x3C4, 0x04); vga_outb(0x3C5, 0x06); /* sequential, no chain */

  /* Graphics controller: read from plane 2, sequential */
  vga_outb(0x3CE, 0x04); vga_outb(0x3CF, 0x02); /* read map = plane 2 */
  vga_outb(0x3CE, 0x05); vga_outb(0x3CF, 0x00); /* mode: sequential */
  vga_outb(0x3CE, 0x06); vga_outb(0x3CF, 0x0C); /* misc: A0000, 64K */
}

/**
 * Restore VGA registers to text-mode settings after font access.
 */
static void vga_font_access_end(void) {
  /* Sequencer: write to planes 0,1; odd/even mode */
  vga_outb(0x3C4, 0x02); vga_outb(0x3C5, 0x03);
  vga_outb(0x3C4, 0x04); vga_outb(0x3C5, 0x02);

  /* Graphics controller: text-mode settings */
  vga_outb(0x3CE, 0x04); vga_outb(0x3CF, 0x00);
  vga_outb(0x3CE, 0x05); vga_outb(0x3CF, 0x10); /* odd/even */
  vga_outb(0x3CE, 0x06); vga_outb(0x3CF, 0x0E); /* B8000, text */
}

static void vga_save_font(void) {
  volatile unsigned char *plane2 = (volatile unsigned char *)0xA0000;
  int i;

  vga_font_access_begin();
  for (i = 0; i < VGA_FONT_SIZE; i++) {
    g_font_save[i] = plane2[i];
  }
  vga_font_access_end();
}

static void vga_restore_font(void) {
  volatile unsigned char *plane2 = (volatile unsigned char *)0xA0000;
  int i;

  vga_font_access_begin();
  for (i = 0; i < VGA_FONT_SIZE; i++) {
    plane2[i] = g_font_save[i];
  }
  vga_font_access_end();
}

int vga_gfx_enter(void) {
  /* Save text-mode font from plane 2 before mode switch corrupts it */
  vga_save_font();

  /* Use BIOS-style VGA register programming to set mode 13h (320x200x256).
   * In a real BIOS environment we'd use int 0x10 AH=0x00 AL=0x13, but
   * since we're in protected mode we program the VGA registers directly. */

  /* Miscellaneous Output Register: select 320x200, 25MHz clock */
  vga_outb(0x3C2, 0x63);

  /* Sequencer registers */
  vga_outb(0x3C4, 0x00); vga_outb(0x3C5, 0x03); /* Reset */
  vga_outb(0x3C4, 0x01); vga_outb(0x3C5, 0x01); /* Clocking mode: 8-dot */
  vga_outb(0x3C4, 0x02); vga_outb(0x3C5, 0x0F); /* Map mask: all planes */
  vga_outb(0x3C4, 0x03); vga_outb(0x3C5, 0x00); /* Character map select */
  vga_outb(0x3C4, 0x04); vga_outb(0x3C5, 0x0E); /* Memory mode: chain-4 */

  /* Unlock CRTC registers */
  vga_outb(0x3D4, 0x11); vga_outb(0x3D5, vga_inb(0x3D5) & 0x7F);

  /* CRTC registers for 320x200 */
  static const unsigned char crtc_regs[] = {
    0x5F, /* 0x00: Horizontal Total */
    0x4F, /* 0x01: Horizontal Display End */
    0x50, /* 0x02: Start Horizontal Blanking */
    0x82, /* 0x03: End Horizontal Blanking */
    0x54, /* 0x04: Start Horizontal Retrace */
    0x80, /* 0x05: End Horizontal Retrace */
    0xBF, /* 0x06: Vertical Total */
    0x1F, /* 0x07: Overflow */
    0x00, /* 0x08: Preset Row Scan */
    0x41, /* 0x09: Maximum Scan Line (double-scan) */
    0x00, /* 0x0A: Cursor Start */
    0x00, /* 0x0B: Cursor End */
    0x00, /* 0x0C: Start Address High */
    0x00, /* 0x0D: Start Address Low */
    0x00, /* 0x0E: Cursor Location High */
    0x00, /* 0x0F: Cursor Location Low */
    0x9C, /* 0x10: Vertical Retrace Start */
    0x0E, /* 0x11: Vertical Retrace End */
    0x8F, /* 0x12: Vertical Display End */
    0x28, /* 0x13: Offset (logical width / 2) */
    0x40, /* 0x14: Underline Location */
    0x96, /* 0x15: Start Vertical Blanking */
    0xB9, /* 0x16: End Vertical Blanking */
    0xA3, /* 0x17: CRTC Mode Control */
    0xFF, /* 0x18: Line Compare */
  };

  int i;
  for (i = 0; i < 25; i++) {
    vga_outb(0x3D4, (unsigned char)i);
    vga_outb(0x3D5, crtc_regs[i]);
  }

  /* Graphics Controller registers */
  vga_outb(0x3CE, 0x00); vga_outb(0x3CF, 0x00); /* Set/Reset */
  vga_outb(0x3CE, 0x01); vga_outb(0x3CF, 0x00); /* Enable Set/Reset */
  vga_outb(0x3CE, 0x02); vga_outb(0x3CF, 0x00); /* Color Compare */
  vga_outb(0x3CE, 0x03); vga_outb(0x3CF, 0x00); /* Data Rotate */
  vga_outb(0x3CE, 0x04); vga_outb(0x3CF, 0x00); /* Read Map Select */
  vga_outb(0x3CE, 0x05); vga_outb(0x3CF, 0x40); /* Graphics Mode: 256-color */
  vga_outb(0x3CE, 0x06); vga_outb(0x3CF, 0x05); /* Misc: chain, A0000 */
  vga_outb(0x3CE, 0x07); vga_outb(0x3CF, 0x0F); /* Color Don't Care */
  vga_outb(0x3CE, 0x08); vga_outb(0x3CF, 0xFF); /* Bit Mask */

  /* Attribute Controller registers */
  (void)vga_inb(0x3DA); /* Reset flip-flop */
  for (i = 0; i < 16; i++) {
    vga_outb(0x3C0, (unsigned char)i);
    vga_outb(0x3C0, (unsigned char)i);
  }
  vga_outb(0x3C0, 0x10); vga_outb(0x3C0, 0x41); /* Mode control */
  vga_outb(0x3C0, 0x11); vga_outb(0x3C0, 0x00); /* Overscan */
  vga_outb(0x3C0, 0x12); vga_outb(0x3C0, 0x0F); /* Color Plane Enable */
  vga_outb(0x3C0, 0x13); vga_outb(0x3C0, 0x00); /* Horizontal Panning */
  vga_outb(0x3C0, 0x14); vga_outb(0x3C0, 0x00); /* Color Select */

  /* Enable display */
  vga_outb(0x3C0, 0x20);

  g_gfx_active = 1;
  vga_gfx_clear(0);
  return 1;
}

int vga_gfx_leave(void) {
  /* Switch back to text mode 03h by programming VGA registers */

  /* Miscellaneous Output Register: text mode timing */
  vga_outb(0x3C2, 0x67);

  /* Sequencer registers for text mode */
  vga_outb(0x3C4, 0x00); vga_outb(0x3C5, 0x03);
  vga_outb(0x3C4, 0x01); vga_outb(0x3C5, 0x00); /* 9-dot characters */
  vga_outb(0x3C4, 0x02); vga_outb(0x3C5, 0x03); /* Planes 0,1 */
  vga_outb(0x3C4, 0x03); vga_outb(0x3C5, 0x00);
  vga_outb(0x3C4, 0x04); vga_outb(0x3C5, 0x02); /* Odd/even mode */

  /* Unlock CRTC */
  vga_outb(0x3D4, 0x11); vga_outb(0x3D5, vga_inb(0x3D5) & 0x7F);

  /* CRTC registers for 80x25 text mode */
  static const unsigned char crtc_text[] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF
  };

  int i;
  for (i = 0; i < 25; i++) {
    vga_outb(0x3D4, (unsigned char)i);
    vga_outb(0x3D5, crtc_text[i]);
  }

  /* Graphics Controller for text mode */
  vga_outb(0x3CE, 0x00); vga_outb(0x3CF, 0x00);
  vga_outb(0x3CE, 0x01); vga_outb(0x3CF, 0x00);
  vga_outb(0x3CE, 0x02); vga_outb(0x3CF, 0x00);
  vga_outb(0x3CE, 0x03); vga_outb(0x3CF, 0x00);
  vga_outb(0x3CE, 0x04); vga_outb(0x3CF, 0x00);
  vga_outb(0x3CE, 0x05); vga_outb(0x3CF, 0x10); /* Odd/even */
  vga_outb(0x3CE, 0x06); vga_outb(0x3CF, 0x0E); /* Text, B8000 */
  vga_outb(0x3CE, 0x07); vga_outb(0x3CF, 0x00);
  vga_outb(0x3CE, 0x08); vga_outb(0x3CF, 0xFF);

  /* Attribute Controller for text mode */
  (void)vga_inb(0x3DA);
  for (i = 0; i < 16; i++) {
    vga_outb(0x3C0, (unsigned char)i);
    vga_outb(0x3C0, (unsigned char)i);
  }
  vga_outb(0x3C0, 0x10); vga_outb(0x3C0, 0x0C); /* Text blink */
  vga_outb(0x3C0, 0x11); vga_outb(0x3C0, 0x00);
  vga_outb(0x3C0, 0x12); vga_outb(0x3C0, 0x0F);
  vga_outb(0x3C0, 0x13); vga_outb(0x3C0, 0x08);
  vga_outb(0x3C0, 0x14); vga_outb(0x3C0, 0x00);
  vga_outb(0x3C0, 0x20);

  g_gfx_active = 0;

  /* Restore the text-mode font into plane 2 */
  vga_restore_font();

  /* Clear text framebuffer and reset cursor via the text driver */
  volatile unsigned short *text_buf = (volatile unsigned short *)0xB8000;
  for (i = 0; i < 80 * 25; i++) {
    text_buf[i] = 0x0720; /* grey-on-black space */
  }
  vga_text_set_cursor(0, 0);

  return 1;
}

int vga_gfx_is_active(void) {
  return g_gfx_active;
}

void vga_gfx_clear(unsigned char color) {
  int i;
  for (i = 0; i < VGA_GFX_FB_SIZE; i++) {
    VGA_GFX_FRAMEBUFFER[i] = color;
  }
}

void vga_gfx_put_pixel(int x, int y, unsigned char color) {
  if (x < 0 || x >= VGA_GFX_WIDTH || y < 0 || y >= VGA_GFX_HEIGHT) {
    return;
  }
  VGA_GFX_FRAMEBUFFER[y * VGA_GFX_WIDTH + x] = color;
}

unsigned char vga_gfx_get_pixel(int x, int y) {
  if (x < 0 || x >= VGA_GFX_WIDTH || y < 0 || y >= VGA_GFX_HEIGHT) {
    return 0;
  }
  return VGA_GFX_FRAMEBUFFER[y * VGA_GFX_WIDTH + x];
}

void vga_gfx_draw_rect(int x, int y, int w, int h, unsigned char color) {
  int row, col;
  int x_end = x + w;
  int y_end = y + h;

  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x_end > VGA_GFX_WIDTH) x_end = VGA_GFX_WIDTH;
  if (y_end > VGA_GFX_HEIGHT) y_end = VGA_GFX_HEIGHT;

  for (row = y; row < y_end; row++) {
    int offset = row * VGA_GFX_WIDTH + x;
    for (col = x; col < x_end; col++) {
      VGA_GFX_FRAMEBUFFER[offset] = color;
      offset++;
    }
  }
}
