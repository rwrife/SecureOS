#ifndef SECUREOS_VGA_GFX_H
#define SECUREOS_VGA_GFX_H

/**
 * @file vga_gfx.h
 * @brief VGA mode 13h (320x200x256) graphics driver.
 *
 * Purpose:
 *   Provides pixel-level drawing in VGA mode 13h. The framebuffer is a
 *   linear 64000-byte region at physical address 0xA0000 where each byte
 *   is one pixel using the standard VGA 256-color palette.
 *
 * Interactions:
 *   - kernel/user/launcher_exec.c: bridge functions call these for
 *     user-space pixel drawing syscalls.
 *   - kernel/drivers/video/vga_text.c: text mode is restored when
 *     switching back from graphics mode.
 *
 * Launched by:
 *   Called from syscall bridge when an app requests graphics mode.
 *   Not a standalone process.
 */

#define VGA_GFX_WIDTH  320
#define VGA_GFX_HEIGHT 200

/**
 * Switch to VGA mode 13h (320x200x256). Clears the screen to black.
 * Returns 1 on success, 0 on failure.
 */
int vga_gfx_enter(void);

/**
 * Switch back to VGA text mode (mode 03h, 80x25).
 * Returns 1 on success, 0 on failure.
 */
int vga_gfx_leave(void);

/**
 * Returns 1 if currently in graphics mode, 0 if in text mode.
 */
int vga_gfx_is_active(void);

/**
 * Clear the entire graphics framebuffer to the given color index.
 */
void vga_gfx_clear(unsigned char color);

/**
 * Draw a single pixel at (x, y) with the given palette color index.
 */
void vga_gfx_put_pixel(int x, int y, unsigned char color);

/**
 * Fill a rectangle from (x, y) with dimensions (w, h) using color.
 */
void vga_gfx_draw_rect(int x, int y, int w, int h, unsigned char color);

/**
 * Read the current color of pixel at (x, y). Returns 0 for out-of-bounds.
 */
unsigned char vga_gfx_get_pixel(int x, int y);

/**
 * Bulk-copy a rectangle of pixels from `pixels` into the framebuffer at
 * (x, y). `pixels` is a tightly packed w x h byte buffer in row-major order.
 * The destination rectangle is clipped to screen bounds; the corresponding
 * region of `pixels` is skipped accordingly. This is the fast path for the
 * window manager compositor, which would otherwise pay one cross-TU
 * function call per pixel (~64K per frame for a full backbuffer flush).
 */
void vga_gfx_blit(int x, int y, int w, int h, const unsigned char *pixels);

#endif
