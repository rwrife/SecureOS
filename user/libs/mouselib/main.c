/**
 * @file main.c
 * @brief mouselib – shared library for mouse input queries.
 *
 * Purpose:
 *   Implements the mouselib user-space API by calling into kernel syscall
 *   stubs that access the mouse HAL. Provides functions for querying mouse
 *   position, button state, and dequeuing discrete events.
 *
 *   If no mouse hardware is present, all functions return zero/inactive
 *   state gracefully—applications do not need special error handling.
 *
 * Interactions:
 *   - lib/mouselib.h: public API definitions.
 *   - kernel/hal/mouse_hal.c: underlying implementation via syscall stubs.
 *   - process.c: the library ELF is loaded by the process subsystem.
 *   - console.c: "loadlib mouselib" loads this into the session.
 *
 * Launched by:
 *   Loaded as a shared library via "loadlib mouselib" at the console.
 *   Built as a standalone ELF binary placed in /lib/.
 */

#include "lib/mouselib.h"

/* Syscall stubs - these will be resolved by the kernel's syscall interface.
 * For now, provide stub implementations that the kernel can intercept. */

static mouselib_state_t g_cached_state;
static int g_state_valid;

int mouselib_available(void) {
  /* In a full implementation, this would syscall into the kernel.
   * The stub version always returns 0 until properly linked. */
  (void)MOUSELIB_HANDLE_INVALID;
  return 0;
}

int mouselib_get_state(mouselib_state_t *state) {
  if (state == 0) {
    return 0;
  }
  if (!mouselib_available()) {
    state->x = 0;
    state->y = 0;
    state->buttons = 0;
    return 0;
  }
  *state = g_cached_state;
  return g_state_valid;
}

int mouselib_poll_event(mouselib_event_t *event) {
  if (event == 0) {
    return 0;
  }
  event->type = MOUSELIB_EVENT_NONE;
  event->x = 0;
  event->y = 0;
  event->button = 0;
  return 0;
}

int mouselib_button_pressed(unsigned char button) {
  if (!mouselib_available()) {
    return 0;
  }
  return (g_cached_state.buttons & button) ? 1 : 0;
}

int mouselib_get_x(void) {
  if (!mouselib_available()) {
    return 0;
  }
  return g_cached_state.x;
}

int mouselib_get_y(void) {
  if (!mouselib_available()) {
    return 0;
  }
  return g_cached_state.y;
}

int main(void) {
  (void)MOUSELIB_HANDLE_INVALID;
  return 0;
}
