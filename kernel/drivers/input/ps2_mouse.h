#ifndef SECUREOS_PS2_MOUSE_H
#define SECUREOS_PS2_MOUSE_H

/**
 * @file ps2_mouse.h
 * @brief PS/2 mouse driver interface.
 *
 * Purpose:
 *   Declares the initialization and polling API for the PS/2 auxiliary
 *   device (mouse) connected through the 8042 keyboard controller.
 *   Reads 3-byte packets and provides delta movement and button state.
 *
 * Interactions:
 *   - hal/mouse_hal.c: registers this driver and polls for mouse events.
 *   - core/kmain.c: calls ps2_mouse_init() during boot (optional).
 *
 * Launched by:
 *   Invoked during kernel startup. If initialization fails, mouse support
 *   is simply unavailable. Not a standalone process; compiled into kernel.
 */

/** Mouse button state flags */
#define PS2_MOUSE_BTN_LEFT   0x01u
#define PS2_MOUSE_BTN_RIGHT  0x02u
#define PS2_MOUSE_BTN_MIDDLE 0x04u

/** Mouse event packet */
typedef struct {
  int dx;           /* horizontal movement delta */
  int dy;           /* vertical movement delta */
  unsigned char buttons; /* button state bitmask */
} ps2_mouse_event_t;

/**
 * Initialize the PS/2 mouse (auxiliary device on 8042 controller).
 * Returns 1 on success, 0 on failure (mouse not present or unresponsive).
 */
int ps2_mouse_init(void);

/**
 * Poll for a complete mouse packet.
 * Returns 1 if an event was read, 0 if no data available.
 */
int ps2_mouse_poll(ps2_mouse_event_t *out_event);

#endif
