/**
 * @file hal_cap_entry.c
 * @brief Capability-gated HAL entrypoints implementation (issue #349).
 *
 * Purpose:
 *   Implements the subject-scoped HAL wrappers declared in
 *   hal_cap_entry.h. Each wrapper invokes the corresponding gate
 *   (cap_gfx_framebuffer_gate / cap_input_keyboard_gate /
 *   cap_input_mouse_gate from #357) before delegating to the underlying
 *   backend-neutral HAL primitive.
 *
 *   This is the call-site half of the issue #349 deny-by-default
 *   contract for the virtual-graphics framebuffer mapping and PS/2
 *   input device queues. The launcher / window manager call these
 *   wrappers on behalf of a launched subject; they never call
 *   video_hal_write / input_hal_try_read_char / mouse_hal_poll_event
 *   directly for subject-facing flows.
 *
 * Interactions:
 *   - kernel/cap/cap_gate.c: the gate trio (#357).
 *   - kernel/hal/video_hal.c, input_hal.c, mouse_hal.c: backend-neutral
 *     primitives, delegated to on the allow path.
 *
 * Launched by:
 *   Not a standalone process. Compiled into the kernel image and the
 *   issue #349 host test suite (win_gfx_callsite_test).
 */

#include "hal_cap_entry.h"

#include "input_hal.h"
#include "mouse_hal.h"
#include "video_hal.h"

#include "../cap/cap_gate.h"

cap_result_t video_hal_write_as(cap_subject_id_t subject_id,
                                const char *message,
                                char *deny_marker_buf,
                                size_t deny_marker_size) {
  cap_result_t r = cap_gfx_framebuffer_gate(subject_id, deny_marker_buf,
                                            deny_marker_size);
  if (r != CAP_OK) {
    return r;
  }
  video_hal_write(message);
  return CAP_OK;
}

cap_result_t input_hal_try_read_char_as(cap_subject_id_t subject_id,
                                        char *out_char,
                                        int *bytes_available_out,
                                        char *deny_marker_buf,
                                        size_t deny_marker_size) {
  cap_result_t r = cap_input_keyboard_gate(subject_id, deny_marker_buf,
                                           deny_marker_size);
  if (r != CAP_OK) {
    return r;
  }
  int got = input_hal_try_read_char(out_char);
  if (bytes_available_out != 0) {
    *bytes_available_out = got;
  }
  return CAP_OK;
}

cap_result_t mouse_hal_poll_event_as(cap_subject_id_t subject_id,
                                     void *out_event,
                                     int *event_available_out,
                                     char *deny_marker_buf,
                                     size_t deny_marker_size) {
  cap_result_t r = cap_input_mouse_gate(subject_id, deny_marker_buf,
                                        deny_marker_size);
  if (r != CAP_OK) {
    return r;
  }
  int got = mouse_hal_poll_event((mouse_event_t *)out_event);
  if (event_available_out != 0) {
    *event_available_out = got;
  }
  return CAP_OK;
}
