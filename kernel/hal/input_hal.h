#ifndef SECUREOS_INPUT_HAL_H
#define SECUREOS_INPUT_HAL_H

/**
 * @file input_hal.h
 * @brief Hardware Abstraction Layer for character input devices.
 *
 * Purpose:
 *   Provides a unified input interface that multiplexes all available
 *   character input sources (serial port, PS/2 keyboard, etc.) into a
 *   single polling API. The console and other kernel subsystems call
 *   input_hal_try_read_char() to get the next available character from
 *   any connected input device.
 *
 * Interactions:
 *   - hal/serial_hal.c: serial input source.
 *   - drivers/input/ps2_keyboard.c: keyboard input source.
 *   - core/console.c: primary consumer of input characters.
 *
 * Launched by:
 *   input_hal_init() is called from kmain during boot.
 *   Not a standalone process; compiled into kernel image.
 */

/**
 * Initialize the input HAL. Call after serial and keyboard drivers are ready.
 * Returns 1 on success.
 */
int input_hal_init(void);

/**
 * Try to read one character from any available input source.
 * Checks keyboard first (for graphics mode), then serial.
 * Returns 1 if a character was read into *out_char, 0 otherwise.
 */
int input_hal_try_read_char(char *out_char);

#endif
