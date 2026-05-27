#ifndef SECUREOS_CAP_GATE_H
#define SECUREOS_CAP_GATE_H

#include <stddef.h>

#include "capability.h"

cap_result_t cap_console_write_gate(cap_subject_id_t subject_id, const char *message, size_t *bytes_written);
cap_result_t cap_serial_write_gate(cap_subject_id_t subject_id, const char *message, size_t *bytes_written);
cap_result_t cap_debug_exit_gate(cap_subject_id_t subject_id, int exit_code);

/*
 * Virtual-graphics + PS/2 HAL gates (issue #349 / BUILD_ROADMAP §5.5+§5.6).
 *
 * Each of these wraps a `cap_check()` against the appropriate cap and
 * emits the canonical `CAP:DENY:<sid>:<cap>:-` marker (per
 * `docs/abi/capability-deny-contract.md`) into the caller-provided buffer
 * on deny. They are deliberately backend-neutral: the framebuffer-map,
 * keyboard-read, and mouse-read entry points in the HAL and the PS/2
 * input + virtual-graphics drivers call these gates before exposing the
 * underlying device byte queue to a launched subject. (HAL files live
 * in kernel/hal and the input/video drivers under kernel/drivers.)
 *
 * `deny_marker_buf` may be NULL when the caller does not want a formatted
 * deny line (e.g. the audit ring is the only consumer). `deny_marker_size`
 * is ignored when `deny_marker_buf` is NULL.
 *
 * On allow (`CAP_OK`), `deny_marker_buf` (if non-NULL) is left untouched.
 * On deny, the buffer receives a NUL-terminated marker line *including*
 * the trailing newline and the canonical `-` resource sentinel (these
 * gates own a *device*, not a per-handle resource — the deny contract
 * §4.3 mandates literal `-` in that case).
 */
cap_result_t cap_gfx_framebuffer_gate(cap_subject_id_t subject_id,
                                      char *deny_marker_buf,
                                      size_t deny_marker_size);
cap_result_t cap_input_keyboard_gate(cap_subject_id_t subject_id,
                                     char *deny_marker_buf,
                                     size_t deny_marker_size);
cap_result_t cap_input_mouse_gate(cap_subject_id_t subject_id,
                                  char *deny_marker_buf,
                                  size_t deny_marker_size);

#endif