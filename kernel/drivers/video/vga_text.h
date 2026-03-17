#ifndef SECUREOS_VGA_TEXT_H
#define SECUREOS_VGA_TEXT_H

/**
 * @file vga_text.h
 * @brief VGA text-mode driver registration helpers.
 *
 * Purpose:
 *   Declares entry points for registering VGA text-mode output as the active
 *   video backend in video HAL.
 *
 * Interactions:
 *   - hal/video_hal.c: receives backend registration.
 *   - core/kmain.c: calls vga_text_init_primary() during boot.
 *
 * Launched by:
 *   Called during kernel initialization, not a standalone process.
 */

int vga_text_init_primary(void);

#endif
