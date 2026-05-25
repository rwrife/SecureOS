/**
 * @file input.h
 * @brief Input dispatch for the SecureOS window manager.
 *
 * Purpose:
 *   Processes mouse and keyboard events each frame. Handles drag-to-move
 *   on title bars, click-to-focus, close button clicks, and routing keyboard
 *   characters to the focused window's session.
 *
 * Interactions:
 *   - main.c: calls input_update() each frame with current mouse/keyboard state.
 *   - window.h: uses hit-testing and focus management.
 *   - secureos_api.h: uses os_session_write_input for keyboard routing.
 *
 * Launched by:
 *   Not standalone. Compiled into win.bin application.
 */

#ifndef WIN_INPUT_H
#define WIN_INPUT_H

/** Initialize input state. */
void input_init(void);

/**
 * Process one frame of input. Call once per event loop iteration.
 * Returns 1 if the WM should exit, 0 otherwise.
 */
int input_update(void);

#endif /* WIN_INPUT_H */
