#ifndef SECUREOS_SESSION_MANAGER_H
#define SECUREOS_SESSION_MANAGER_H

#include <stddef.h>

#include "../cap/capability.h"

void session_manager_start(cap_subject_id_t bootstrap_subject_id);
int session_manager_create(cap_subject_id_t subject_id, unsigned int *out_session_id);
void session_manager_destroy(unsigned int session_id);
int session_manager_switch(unsigned int session_id);
unsigned int session_manager_active_id(void);
size_t session_manager_list(char *out_buffer, size_t out_buffer_size);

/**
 * Read new output from a session's screen history since the last read.
 * Copies up to out_buffer_size-1 bytes and null-terminates.
 * Returns number of bytes written (0 if nothing new).
 */
size_t session_manager_read_output(unsigned int session_id, char *out_buffer,
                                   size_t out_buffer_size);

/**
 * Inject input characters into a session's input queue.
 * The session's console loop will drain these as if typed on the keyboard.
 * Returns number of characters actually injected.
 */
size_t session_manager_write_input(unsigned int session_id, const char *input,
                                   size_t len);

/**
 * Process pending injected input for a session. Binds the session's console
 * context, drains its inject buffer through the command processor, then
 * restores the previously active context. Called by the window manager
 * each frame to drive session execution.
 */
void session_manager_tick(unsigned int session_id);

/**
 * Mark a session as WM-managed. Auth prompts for this session will go
 * through the event bus (AUTH_PROMPT/AUTH_RESPONSE) instead of blocking
 * on inline console input.
 */
void session_manager_set_wm_managed(unsigned int session_id, int managed);

/**
 * Check if a session is WM-managed. Returns 1 if managed, 0 otherwise.
 */
int session_manager_is_wm_managed(unsigned int session_id);

/**
 * Get the graphics mode for a session: 0 = text, 1 = graphics.
 * Returns -1 if the session does not exist.
 */
int session_manager_get_gfx_mode(unsigned int session_id);

/**
 * Set the graphics mode for a WM-managed session.
 * When gfx_mode is 1, video calls redirect to the session's virtual framebuffer.
 */
void session_manager_set_gfx_mode(unsigned int session_id, int gfx_mode);

/**
 * Clear the session's virtual framebuffer to black (all zeros).
 * Used when a WM-managed app calls video_clear in graphics mode.
 */
void session_manager_clear_vfb(unsigned int session_id);

/**
 * Set virtual mouse state for a WM-managed session.
 * Called by the window manager to inject mouse coordinates relative to
 * the session's virtual framebuffer. Child apps read this instead of
 * raw hardware mouse state.
 */
void session_manager_set_virtual_mouse(unsigned int session_id,
                                       int x, int y,
                                       unsigned char buttons);

/**
 * Get virtual mouse state for a WM-managed session.
 * Bridge functions call this to provide mouse data to child apps.
 */
void session_manager_get_virtual_mouse(unsigned int session_id,
                                       int *out_x, int *out_y,
                                       unsigned char *out_buttons);

/**
 * Get a pointer to the session's virtual framebuffer (320x200).
 * Returns NULL if session doesn't exist or has no VFB allocated.
 * Allocates a VFB from the pool if the session is WM-managed and doesn't
 * have one yet.
 */
unsigned char *session_manager_get_vfb(unsigned int session_id);

/**
 * Set the VFB pixel dimensions for a session. Must be called BEFORE the
 * VFB is first allocated (before set_wm_managed or first VFB access).
 * The WM calls this to match the window content area dimensions.
 */
void session_manager_set_vfb_size(unsigned int session_id,
                                  unsigned int width, unsigned int height);

/**
 * Get the VFB pixel dimensions for a session.
 * Returns defaults (320x200) if not explicitly set.
 */
void session_manager_get_vfb_size(unsigned int session_id,
                                  unsigned int *out_width,
                                  unsigned int *out_height);

/**
 * Read a rectangular region from the session's virtual framebuffer.
 * Copies pixels row-by-row into out_pixels.
 * Returns number of bytes written, or 0 on failure.
 */
size_t session_manager_read_vfb(unsigned int session_id,
                                unsigned char *out_pixels,
                                unsigned int x, unsigned int y,
                                unsigned int w, unsigned int h);

/**
 * Write a single character into the session's VFB at the text cursor position.
 * Handles newline, carriage return, tab, and line wrapping/scrolling.
 * Used by the console to render text output for WM-managed sessions.
 */
void session_manager_vfb_putchar(unsigned int session_id, char ch);

/**
 * Write a null-terminated string into the session's VFB.
 * Renders each character at the text cursor, advancing as needed.
 */
void session_manager_vfb_write(unsigned int session_id, const char *text);

/**
 * Yield from inside a tick when a WM-managed session is blocked (e.g. on auth).
 * Saves the blocked context and returns to session_manager_tick which then
 * returns to the WM so it can continue its event loop.
 * Returns 1 when resumed (auth responded), 0 if yield not possible.
 */
int session_manager_tick_yield(void);

/**
 * Check if a session is currently blocked (yielded mid-command).
 */
int session_manager_is_blocked(unsigned int session_id);

/**
 * M5-SUBSTRATE-005a (issue #350, plan
 * plans/2026-05-26-m5-wm-cascade-on-substrate.md):
 *
 * Deterministic slot-order enumerator predicate that returns the
 * lowest-indexed in-use session owned by `subject`. The cascade
 * orchestrator in `broker_svc_delete_owner` (step 3.5) will call this
 * in a bounded loop, destroying each match, until the predicate misses
 * -- mirroring the M5 cap_handle bulk-revoke discipline.
 *
 * Returns:
 *   0  on hit; writes the session id into *out_session_id when non-NULL.
 *  -1  on miss (no in-use session has subject_id == subject); the out
 *      param is left untouched.
 *
 * Bounds:
 *   - Pure read of g_sessions[]. No allocation, no recursion, no IPC.
 *   - O(SESSION_MAX) per call; the cascade drives at most one destroy
 *     per call, so the outer loop is O(SESSION_MAX^2) in the worst case,
 *     which is fine for the v0 SESSION_MAX = 8.
 *
 * Idempotence:
 *   After destroying a returned session id, re-calling this with the
 *   same subject immediately advances to the next live slot (the freed
 *   slot's in_use bit is cleared, so the scan skips it). When no more
 *   sessions are owned by `subject`, the predicate returns -1 and the
 *   cascade loop exits cleanly.
 */
int session_manager_first_session_for_subject(cap_subject_id_t subject,
                                              unsigned int *out_session_id);

#endif
