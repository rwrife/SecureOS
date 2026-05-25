/**
 * @file vfb_font.h
 * @brief Kernel-side bitmap font for rendering text into session VFBs.
 *
 * Purpose:
 *   Provides a minimal 5x7 fixed-width bitmap font that the kernel uses
 *   to render console text output into a session's virtual framebuffer.
 *   When a WM-managed session produces text, the kernel renders it as
 *   pixels so the window manager can simply read the pixel buffer.
 *
 * Interactions:
 *   - session_manager.c: calls vfb_font_putchar/vfb_font_write to render text
 *   - console.c: text output for WM-managed sessions triggers VFB rendering
 *
 * Launched by:
 *   Compiled into the kernel image. Not standalone.
 */

#ifndef SECUREOS_VFB_FONT_H
#define SECUREOS_VFB_FONT_H

#define VFB_FONT_W 5
#define VFB_FONT_H 7
#define VFB_FONT_SPACING 1  /* 1px gap between characters */

/**
 * Draw a single character glyph at pixel position (px, py) into a buffer.
 * buffer: destination pixel buffer (row-major, 1 byte per pixel)
 * stride: bytes per row in buffer
 * color: palette index for foreground pixels (background not touched)
 */
void vfb_font_draw_char(unsigned char *buffer, int stride,
                        int px, int py, char ch, unsigned char color);

/**
 * Draw a null-terminated string at (px, py).
 * Characters are spaced (VFB_FONT_W + VFB_FONT_SPACING) pixels apart.
 */
void vfb_font_draw_string(unsigned char *buffer, int stride,
                          int px, int py, const char *str,
                          unsigned char color);

#endif /* SECUREOS_VFB_FONT_H */
