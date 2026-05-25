/**
 * @file compositor.h
 * @brief Screen compositor for the SecureOS window manager.
 *
 * Purpose:
 *   Manages a 320x200 back-buffer and composites all visible windows onto it.
 *   Draws title bars, window content (text rendered via bitmap font), borders,
 *   the desktop background, and the mouse cursor. The final composed image is
 *   flushed to the physical VGA framebuffer via os_video_blit.
 *
 * Interactions:
 *   - main.c: calls compositor_render() each frame.
 *   - window.h: reads window table for positions, text buffers, focus state.
 *   - font.h: uses font_draw_char/font_draw_string for text rendering.
 *
 * Launched by:
 *   Not standalone. Compiled into win.bin application.
 */

#ifndef WIN_COMPOSITOR_H
#define WIN_COMPOSITOR_H

/** Initialize compositor (clear back-buffer). */
void compositor_init(void);

/** Render all windows and cursor to the back-buffer, then flush to screen.
 *  mouse_x, mouse_y: current cursor position. */
void compositor_render(int mouse_x, int mouse_y);

#endif /* WIN_COMPOSITOR_H */
