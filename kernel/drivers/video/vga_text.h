#ifndef SECUREOS_VGA_TEXT_H
#define SECUREOS_VGA_TEXT_H

/**
 * @file vga_text.h
 * @brief VGA text-mode driver registration helpers.
 *
 * Purpose:
 *   Declares entry points for registering VGA text-mode output as the active
 *   video backend in video HAL, plus mouse cursor overlay functions.
 *
 * Interactions:
 *   - hal/video_hal.c: receives backend registration.
 *   - hal/mouse_hal.c: calls mouse cursor show/hide.
 *   - core/kmain.c: calls vga_text_init_primary() during boot.
 *
 * Launched by:
 *   Called during kernel initialization, not a standalone process.
 */

int vga_text_init_primary(void);

/**
 * Show the mouse cursor at the given text-mode cell (col, row).
 * Inverts the cell colors to indicate cursor position.
 */
void vga_text_mouse_cursor_show(int col, int row);

/**
 * Hide the mouse cursor, restoring the original cell content.
 */
void vga_text_mouse_cursor_hide(void);

#endif
