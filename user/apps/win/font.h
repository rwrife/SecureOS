/**
 * @file font.h
 * @brief Minimal 5x7 bitmap font for the window manager.
 *
 * Purpose:
 *   Provides a fixed-width 5x7 pixel bitmap font covering printable ASCII
 *   characters (0x20–0x7E). Each character is stored as 7 bytes (one per row),
 *   with the lowest 5 bits representing pixels (MSB=left, LSB=right).
 *
 * Interactions:
 *   - compositor.c: uses font_draw_char/font_draw_string to render terminal text.
 *   - titlebar.c: uses font_draw_string to render window titles.
 *
 * Launched by:
 *   Not standalone. Compiled into win.bin application.
 */

#ifndef WIN_FONT_H
#define WIN_FONT_H

#define FONT_CHAR_W 5
#define FONT_CHAR_H 7

/**
 * Draw a single character at pixel position (px, py) into a buffer.
 * The buffer is row-major with stride `buf_stride` bytes per row.
 * `color` is the palette index for lit pixels; background is not drawn.
 */
void font_draw_char(unsigned char *buffer, int buf_stride,
                    int px, int py, char ch, unsigned char color);

/**
 * Draw a null-terminated string starting at (px, py).
 * Characters are spaced FONT_CHAR_W+1 pixels apart horizontally.
 */
void font_draw_string(unsigned char *buffer, int buf_stride,
                      int px, int py, const char *str, unsigned char color);

#endif /* WIN_FONT_H */
