#ifndef SECUREOS_MOUSE_HAL_H
#define SECUREOS_MOUSE_HAL_H

/**
 * @file mouse_hal.h
 * @brief Hardware Abstraction Layer for mouse input devices.
 *
 * Purpose:
 *   Provides a unified mouse interface that tracks cursor position,
 *   button state, and click events. Abstracts the underlying mouse
 *   hardware (PS/2, USB, etc.) from consumers. The mouse is optional—
 *   if no mouse driver initializes successfully, all query functions
 *   return zero/inactive state and the cursor is not rendered.
 *
 * Interactions:
 *   - drivers/input/ps2_mouse.c: provides mouse event data.
 *   - drivers/video/vga_text.c: renders the text-mode cursor overlay.
 *   - core/console.c: calls mouse_hal_update() in the main loop.
 *   - user/libs/mouselib: user-space library queries mouse state.
 *
 * Launched by:
 *   mouse_hal_init() is called from kmain during boot.
 *   Not a standalone process; compiled into kernel image.
 */

/** Mouse button identifiers */
#define MOUSE_BTN_LEFT   0x01u
#define MOUSE_BTN_RIGHT  0x02u
#define MOUSE_BTN_MIDDLE 0x04u

/** Mouse click event types */
typedef enum {
  MOUSE_EVENT_NONE = 0,
  MOUSE_EVENT_MOVE = 1,
  MOUSE_EVENT_BUTTON_DOWN = 2,
  MOUSE_EVENT_BUTTON_UP = 3,
} mouse_event_type_t;

/** Snapshot of the current mouse state */
typedef struct {
  int x;                  /* cursor X position (pixel/column) */
  int y;                  /* cursor Y position (pixel/row) */
  unsigned char buttons;  /* currently held button bitmask */
} mouse_state_t;

/** A discrete mouse event for event-driven consumers */
typedef struct {
  mouse_event_type_t type;
  int x;
  int y;
  unsigned char button; /* which button changed (for button events) */
} mouse_event_t;

/**
 * Initialize the mouse HAL. Attempts to enable the PS/2 mouse driver.
 * Returns 1 if mouse is available, 0 if not (non-fatal).
 */
int mouse_hal_init(void);

/**
 * Returns 1 if a mouse is present and active, 0 otherwise.
 */
int mouse_hal_available(void);

/**
 * Poll the mouse hardware and update internal state.
 * Call this periodically (e.g., from the console main loop).
 * Also updates the VGA cursor overlay if in graphics mode.
 */
void mouse_hal_update(void);

/**
 * Get the current mouse state (position + buttons).
 */
void mouse_hal_get_state(mouse_state_t *out_state);

/**
 * Dequeue the next mouse event. Returns 1 if an event was available.
 */
int mouse_hal_poll_event(mouse_event_t *out_event);

/**
 * Set the screen bounds for cursor clamping.
 * width/height are in text-mode columns/rows.
 */
void mouse_hal_set_bounds(int width, int height);

#endif
