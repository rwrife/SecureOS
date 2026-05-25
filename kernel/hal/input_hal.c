/**
 * @file input_hal.c
 * @brief Hardware Abstraction Layer for character input devices.
 *
 * Purpose:
 *   Multiplexes all available character input sources (PS/2 keyboard,
 *   serial port) into a single polling interface. The console calls
 *   input_hal_try_read_char() which checks each source in priority order
 *   and returns the first available character.
 *
 * Interactions:
 *   - drivers/input/ps2_keyboard.c: polled for keyboard scancodes.
 *   - hal/serial_hal.c: polled for serial input (used in console mode).
 *   - core/console.c: calls input_hal_try_read_char() in main loop.
 *
 * Launched by:
 *   input_hal_init() is called from kmain during boot after keyboard
 *   and serial drivers are initialized. Not a standalone process.
 */

#include "input_hal.h"

#include "../drivers/input/ps2_keyboard.h"
#include "serial_hal.h"

static int g_input_initialized;

int input_hal_init(void) {
  g_input_initialized = 1;
  return 1;
}

int input_hal_try_read_char(char *out_char) {
  if (out_char == 0 || !g_input_initialized) {
    return 0;
  }

  /* Try PS/2 keyboard first (needed for graphics mode) */
  if (ps2_keyboard_try_read_char(out_char)) {
    return 1;
  }

  /* Fall back to serial input (works in console mode) */
  if (serial_hal_try_read_char(out_char)) {
    return 1;
  }

  return 0;
}
