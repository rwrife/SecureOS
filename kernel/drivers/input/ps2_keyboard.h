#ifndef SECUREOS_PS2_KEYBOARD_H
#define SECUREOS_PS2_KEYBOARD_H

/**
 * @file ps2_keyboard.h
 * @brief PS/2 keyboard driver interface.
 *
 * Purpose:
 *   Declares the initialization and polling API for the PS/2 keyboard
 *   controller. Translates scan codes from the 8042 controller into ASCII
 *   characters that can be consumed by the input HAL.
 *
 * Interactions:
 *   - hal/input_hal.c: registers this driver as an input source.
 *   - core/kmain.c: calls ps2_keyboard_init() during boot.
 *
 * Launched by:
 *   Invoked during kernel startup. Not a standalone process; compiled into
 *   the kernel image.
 */

/**
 * Initialize the PS/2 keyboard controller.
 * Returns 1 on success, 0 on failure.
 */
int ps2_keyboard_init(void);

/**
 * Try to read one ASCII character from the keyboard buffer.
 * Returns 1 if a character was read, 0 if no key is available.
 */
int ps2_keyboard_try_read_char(char *out_char);

#endif
