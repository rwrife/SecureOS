/**
 * @file auth_dialog.h
 * @brief Auth prompt dialog for the SecureOS window manager.
 *
 * Purpose:
 *   Polls for pending auth prompts from the kernel's capability system and
 *   renders a modal dialog overlay with Allow/Deny buttons. When the user
 *   clicks a button, responds to the kernel via os_auth_respond.
 *
 * Interactions:
 *   - secureos_api.h: os_auth_poll_prompt, os_auth_respond
 *   - main.c: called each frame from the main loop
 *   - compositor.c: renders after windows but before cursor
 *
 * Launched by:
 *   Not standalone. Compiled into win.bin application.
 */
#ifndef WIN_AUTH_DIALOG_H
#define WIN_AUTH_DIALOG_H

/**
 * Poll for pending auth prompts. If one is active, store it internally.
 * Returns 1 if a dialog is currently showing, 0 otherwise.
 */
int auth_dialog_poll(void);

/**
 * Check if the auth dialog is currently active (blocking input to windows).
 */
int auth_dialog_active(void);

/**
 * Process a mouse click against the auth dialog buttons.
 * Returns 1 if the click was consumed by the dialog, 0 otherwise.
 */
int auth_dialog_click(int mx, int my);

/**
 * Render the auth dialog into the backbuffer.
 * Call after window rendering but before cursor.
 */
void auth_dialog_render(unsigned char *backbuffer, int screen_w, int screen_h);

#endif /* WIN_AUTH_DIALOG_H */
