/**
 * @file cap_gate.c
 * @brief Capability-gated entry points for privileged kernel operations.
 *
 * Purpose:
 *   Implements gate functions that check a subject's capabilities before
 *   allowing access to protected resources.  Each gate wraps a specific
 *   operation (console write, serial write, debug exit, disk I/O) and
 *   returns a capability result indicating whether the caller is
 *   authorized.
 *
 * Interactions:
 *   - cap_table.c: each gate calls cap_check() to verify that the
 *     subject holds the required capability token.
 *   - console.c: invokes cap_console_write_gate and cap_debug_exit_gate
 *     for gated console operations.
 *   - serial.c: cap_serial_write_gate controls writes to the serial
 *     port.
 *   - event_bus.c: cap_audit_read_gate protects audit log access.
 *
 * Launched by:
 *   Not a standalone process.  Gate functions are called on demand by
 *   kernel subsystems.  Compiled into the kernel image.
 */

#include "cap_gate.h"

#include "cap_deny_marker.h"

static size_t cap_message_len(const char *message) {
  size_t len = 0u;
  if (message == 0) {
    return 0u;
  }

  while (message[len] != '\0') {
    ++len;
  }

  return len;
}

static cap_result_t cap_write_gate(cap_subject_id_t subject_id,
                                   capability_id_t required_capability,
                                   const char *message,
                                   size_t *bytes_written) {
  cap_result_t check_result = cap_check(subject_id, required_capability);
  if (check_result != CAP_OK) {
    return check_result;
  }

  if (bytes_written != NULL) {
    *bytes_written = cap_message_len(message);
  }

  return CAP_OK;
}

cap_result_t cap_console_write_gate(cap_subject_id_t subject_id, const char *message, size_t *bytes_written) {
  return cap_write_gate(subject_id, CAP_CONSOLE_WRITE, message, bytes_written);
}

cap_result_t cap_serial_write_gate(cap_subject_id_t subject_id, const char *message, size_t *bytes_written) {
  return cap_write_gate(subject_id, CAP_SERIAL_WRITE, message, bytes_written);
}

cap_result_t cap_debug_exit_gate(cap_subject_id_t subject_id, int exit_code) {
  (void)exit_code;
  return cap_check(subject_id, CAP_DEBUG_EXIT);
}

static cap_result_t cap_device_gate(cap_subject_id_t subject_id,
                                    capability_id_t cap_id,
                                    char *deny_marker_buf,
                                    size_t deny_marker_size) {
  cap_result_t check_result = cap_check(subject_id, cap_id);
  if (check_result == CAP_OK) {
    return CAP_OK;
  }

  /* Emit canonical CAP:DENY:<sid>:<cap>:-\n marker into caller buffer
   * (deny-by-default invariant, README §"Design Principles"). The kernel
   * caller is responsible for flushing the marker to the serial backend
   * so it lands on the boot transcript scraped by the _qemu peers.
   * Resource sentinel is the literal '-' per
   * docs/abi/capability-deny-contract.md §4.3 (no handle has been
   * minted at the gate boundary). */
  if (deny_marker_buf != (char *)0 && deny_marker_size > 0u) {
    int formatted = cap_deny_marker_format(subject_id, cap_id, "-",
                                           deny_marker_buf,
                                           deny_marker_size);
    if (formatted < 0 && deny_marker_size > 0u) {
      /* Buffer too small — leave it empty-terminated so callers can
       * still safely strlen() it; the audit ring (cap_check above
       * already recorded the deny) remains the source of truth. */
      deny_marker_buf[0] = '\0';
    }
  }

  return check_result;
}

cap_result_t cap_gfx_framebuffer_gate(cap_subject_id_t subject_id,
                                      char *deny_marker_buf,
                                      size_t deny_marker_size) {
  return cap_device_gate(subject_id, CAP_GFX_FRAMEBUFFER,
                         deny_marker_buf, deny_marker_size);
}

cap_result_t cap_input_keyboard_gate(cap_subject_id_t subject_id,
                                     char *deny_marker_buf,
                                     size_t deny_marker_size) {
  return cap_device_gate(subject_id, CAP_INPUT_KEYBOARD,
                         deny_marker_buf, deny_marker_size);
}

cap_result_t cap_input_mouse_gate(cap_subject_id_t subject_id,
                                  char *deny_marker_buf,
                                  size_t deny_marker_size) {
  return cap_device_gate(subject_id, CAP_INPUT_MOUSE,
                         deny_marker_buf, deny_marker_size);
}
