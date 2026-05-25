/**
 * @file mouselib.h
 * @brief User-space library for querying mouse position and click events.
 *
 * Purpose:
 *   Provides an API for user-space applications to query the current mouse
 *   cursor position, button state, and dequeue discrete click/move events.
 *   Applications load this library via "loadlib mouselib" and use the
 *   functions below to integrate mouse interaction.
 *
 *   If no mouse hardware is present, all query functions return zero/empty
 *   state gracefully.
 *
 * Interactions:
 *   - kernel/hal/mouse_hal.c: provides the underlying mouse state (accessed
 *     through syscall stubs in the user runtime).
 *   - User applications: include this header and link against mouselib.
 *
 * Launched by:
 *   Header-only definitions + loaded as a shared library via
 *   "loadlib mouselib" at the console.
 */

#ifndef SECUREOS_MOUSELIB_H
#define SECUREOS_MOUSELIB_H

#ifdef __cplusplus
extern "C" {
#endif

/** Mouse button constants */
#define MOUSELIB_BTN_LEFT   0x01u
#define MOUSELIB_BTN_RIGHT  0x02u
#define MOUSELIB_BTN_MIDDLE 0x04u

/** Mouse event types */
typedef enum {
  MOUSELIB_EVENT_NONE = 0,
  MOUSELIB_EVENT_MOVE = 1,
  MOUSELIB_EVENT_BUTTON_DOWN = 2,
  MOUSELIB_EVENT_BUTTON_UP = 3,
} mouselib_event_type_t;

/** Current mouse state snapshot */
typedef struct {
  int x;                  /* cursor column (0-based) */
  int y;                  /* cursor row (0-based) */
  unsigned char buttons;  /* button bitmask (MOUSELIB_BTN_*) */
} mouselib_state_t;

/** Discrete mouse event */
typedef struct {
  mouselib_event_type_t type;
  int x;                  /* position at time of event */
  int y;
  unsigned char button;   /* which button (for button events) */
} mouselib_event_t;

/** Library handle sentinel */
#define MOUSELIB_HANDLE_INVALID 0u

/**
 * Check if mouse hardware is available.
 * Returns 1 if a mouse is present, 0 otherwise.
 */
int mouselib_available(void);

/**
 * Get the current mouse state (position + button state).
 * Populates *state. Returns 1 on success, 0 if mouse unavailable.
 */
int mouselib_get_state(mouselib_state_t *state);

/**
 * Poll for the next mouse event.
 * Returns 1 if an event was dequeued into *event, 0 if queue is empty.
 */
int mouselib_poll_event(mouselib_event_t *event);

/**
 * Check if a specific button is currently pressed.
 * button: one of MOUSELIB_BTN_LEFT, MOUSELIB_BTN_RIGHT, MOUSELIB_BTN_MIDDLE.
 * Returns 1 if pressed, 0 if not (or mouse unavailable).
 */
int mouselib_button_pressed(unsigned char button);

/**
 * Get the current cursor X position (column).
 */
int mouselib_get_x(void);

/**
 * Get the current cursor Y position (row).
 */
int mouselib_get_y(void);

#ifdef __cplusplus
}
#endif

#endif
