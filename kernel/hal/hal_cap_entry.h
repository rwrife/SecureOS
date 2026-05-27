#ifndef SECUREOS_HAL_CAP_ENTRY_H
#define SECUREOS_HAL_CAP_ENTRY_H

/**
 * @file hal_cap_entry.h
 * @brief Capability-gated HAL entrypoints (issue #349).
 *
 * Purpose:
 *   Declares the subject-scoped wrappers that the launcher / window
 *   manager / session manager MUST call when exposing the virtual
 *   graphics framebuffer mapping, the PS/2 keyboard byte queue, or
 *   the PS/2 mouse byte queue to a launched subject.
 *
 *   These wrappers are the call-site half of the gate primitive trio
 *   landed in #357:
 *       cap_gfx_framebuffer_gate
 *       cap_input_keyboard_gate
 *       cap_input_mouse_gate
 *
 *   Each wrapper invokes the corresponding gate, and on CAP_OK
 *   delegates to the underlying backend-neutral HAL function
 *   (`video_hal_write`, `input_hal_try_read_char`,
 *   `mouse_hal_poll_event`). On deny the wrapper returns
 *   without touching the caller's output buffer, populates the
 *   canonical `CAP:DENY:<sid>:<cap>:-\n` marker per
 *   docs/abi/capability-deny-contract.md \u00a74.3 (when
 *   deny_marker_buf is non-NULL), and surfaces the underlying
 *   cap_result_t via deny_result_out.
 *
 *   Separating these from input_hal.h / video_hal.h / mouse_hal.h keeps
 *   the architecture-neutral HAL surface free of capability-table
 *   dependencies (consumers that only need character output --
 *   e.g. the boot console -- do not get a transitive include of the
 *   capability subsystem).
 *
 * Interactions:
 *   - kernel/cap/cap_gate.c: invokes cap_gfx_framebuffer_gate /
 *     cap_input_keyboard_gate / cap_input_mouse_gate.
 *   - kernel/hal/video_hal.c, input_hal.c, mouse_hal.c: the underlying
 *     backend-neutral primitives that are delegated to on allow.
 *
 * Launched by:
 *   Not a standalone process. Called from launcher / window-manager
 *   service code on the subject-facing HAL boundary.
 */

#include <stddef.h>

#include "../cap/capability.h"

/* ----------------------------------------------------------------
 * Video HAL (framebuffer surface)
 * --------------------------------------------------------------*/

/**
 * Subject-scoped video write entrypoint.
 *
 * Gates on CAP_GFX_FRAMEBUFFER. On allow, delegates to
 * video_hal_write(message) and returns CAP_OK. On deny, returns the
 * underlying cap_result_t (typically CAP_ERR_MISSING) and -- when
 * deny_marker_buf is non-NULL and deny_marker_size > 0 -- populates the
 * canonical CAP:DENY:<sid>:gfx_framebuffer:-\n marker. The audit ring
 * records the CAP_AUDIT_OP_CHECK regardless of the deny_marker_buf
 * value.
 */
cap_result_t video_hal_write_as(cap_subject_id_t subject_id,
                                const char *message,
                                char *deny_marker_buf,
                                size_t deny_marker_size);

/* ----------------------------------------------------------------
 * Input HAL (PS/2 keyboard byte queue)
 * --------------------------------------------------------------*/

/**
 * Subject-scoped character read entrypoint.
 *
 * Gates on CAP_INPUT_KEYBOARD. On allow, delegates to
 * input_hal_try_read_char(out_char) and returns CAP_OK; the caller
 * inspects *bytes_available_out (1 = a byte was read, 0 = queue was
 * empty). On deny, returns the underlying cap_result_t, leaves
 * *out_char and *bytes_available_out untouched, and populates the
 * canonical CAP:DENY:<sid>:input_keyboard:-\n marker when
 * deny_marker_buf is non-NULL.
 */
cap_result_t input_hal_try_read_char_as(cap_subject_id_t subject_id,
                                        char *out_char,
                                        int *bytes_available_out,
                                        char *deny_marker_buf,
                                        size_t deny_marker_size);

/* ----------------------------------------------------------------
 * Mouse HAL (PS/2 mouse byte queue)
 * --------------------------------------------------------------*/

/* Forward declaration: keep mouse_event_t opaque so consumers that
 * only need the keyboard gate need not pull in mouse_hal.h.
 * Consumers of mouse_hal_poll_event_as must include mouse_hal.h. */
struct mouse_event_t_fwd_;
typedef struct mouse_event_t_fwd_ mouse_event_t_fwd;

/**
 * Subject-scoped mouse event drain entrypoint.
 *
 * Gates on CAP_INPUT_MOUSE. On allow, delegates to
 * mouse_hal_poll_event(out_event) and returns CAP_OK; the caller
 * inspects *event_available_out (1 = event consumed, 0 = queue empty).
 * On deny, returns the underlying cap_result_t, leaves *out_event and
 * *event_available_out untouched, and populates the canonical
 * CAP:DENY:<sid>:input_mouse:-\n marker when deny_marker_buf is
 * non-NULL.
 *
 * out_event is typed as void* here to keep this header backend-neutral;
 * consumers pass a `mouse_event_t *` from mouse_hal.h.
 */
cap_result_t mouse_hal_poll_event_as(cap_subject_id_t subject_id,
                                     void *out_event,
                                     int *event_available_out,
                                     char *deny_marker_buf,
                                     size_t deny_marker_size);

#endif
