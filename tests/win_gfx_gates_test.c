/**
 * @file win_gfx_gates_test.c
 * @brief Allow + deny acceptance for the virtual-graphics / PS-2 HAL gates.
 *
 * Purpose:
 *   Issue #349 (BUILD_ROADMAP \u00a75.5/\u00a75.6, follow-up to #348). Locks down
 *   the kernel-side capability gates that the virtual-graphics HAL,
 *   PS/2 keyboard driver, and PS/2 mouse driver MUST call before
 *   exposing their byte queue / framebuffer mapping to a launched
 *   subject:
 *
 *     - cap_gfx_framebuffer_gate  -> CAP_GFX_FRAMEBUFFER
 *     - cap_input_keyboard_gate   -> CAP_INPUT_KEYBOARD
 *     - cap_input_mouse_gate      -> CAP_INPUT_MOUSE
 *
 *   Mirrors the helloapp_{allow,deny} pattern from \u00a75.2 (#92): one
 *   launcher actor with CAP_CAPABILITY_ADMIN, one app subject with the
 *   relevant grant, one without. We assert:
 *
 *     1. Allow path: gate returns CAP_OK; no CAP:DENY marker is
 *        produced into the caller buffer; audit ring records a
 *        CAP_AUDIT_OP_CHECK with result CAP_OK.
 *     2. Deny path: gate returns CAP_ERR_MISSING; the caller buffer
 *        contains the canonical "CAP:DENY:<sid>:<cap>:-\\n" marker
 *        (cap_deny_marker_validate accepts it); audit ring records
 *        a CAP_AUDIT_OP_CHECK with result CAP_ERR_MISSING.
 *
 *   The full HAL + driver call-site wiring (video_hal.c, ps2_keyboard.c,
 *   ps2_mouse.c) and the matching _qemu peers (win_gfx_allow_qemu /
 *   win_gfx_deny_qemu) are tracked as follow-ups; this test pins the
 *   gate contract those call sites will consume, in the same way the
 *   helloapp_{allow,deny} host tests pinned cap_console_write_gate
 *   before the launcher_console_qemu peer landed.
 *
 * Interactions:
 *   - kernel/cap/cap_gate.c (gate implementations)
 *   - kernel/cap/cap_deny_marker.c (marker formatter + validator)
 *   - kernel/cap/capability.c, cap_table.c (audit ring + grant/check)
 *
 * Launched by:
 *   build/scripts/test_win_gfx_gates.sh, dispatched via test.sh.
 *
 * Issue: #349.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_deny_marker.h"
#include "../kernel/cap/cap_gate.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"

#define DENY_BUF_BYTES 128u

static const cap_subject_id_t LAUNCHER_ROOT_SUBJECT = 0u;
static const cap_subject_id_t LAUNCHER_SUBJECT = 1u;
static const cap_subject_id_t WIN_APP_SUBJECT = 3u;
static const cap_subject_id_t DENIED_APP_SUBJECT = 4u;

static void die(const char *reason) {
  printf("TEST:FAIL:win_gfx_gates:%s\n", reason);
  exit(1);
}

static int audit_ring_contains(cap_subject_id_t subject_id,
                               capability_id_t capability_id,
                               cap_result_t expected_result) {
  size_t count = cap_audit_count_for_tests();
  for (size_t i = 0u; i < count; ++i) {
    cap_audit_event_t event = {0};
    if (cap_audit_get_for_tests(i, &event) != CAP_OK) {
      continue;
    }
    if (event.operation == CAP_AUDIT_OP_CHECK &&
        event.subject_id == subject_id &&
        event.capability_id == capability_id &&
        event.result == expected_result) {
      return 1;
    }
  }
  return 0;
}

typedef cap_result_t (*device_gate_fn)(cap_subject_id_t,
                                       char *deny_marker_buf,
                                       size_t deny_marker_size);

static void exercise_gate_allow(const char *label,
                                device_gate_fn gate,
                                capability_id_t cap_id,
                                cap_subject_id_t subject) {
  char buf[DENY_BUF_BYTES];
  memset(buf, 0x7F, sizeof(buf)); /* sentinel: gate must not overwrite */

  cap_result_t r = gate(subject, buf, sizeof(buf));
  if (r != CAP_OK) {
    fprintf(stderr, "allow-path label=%s expected CAP_OK got %d\n", label, r);
    die("allow_not_ok");
  }
  /* Defense: no deny marker leaks on the allow path. */
  if (memchr(buf, 'C', 1) != NULL && memcmp(buf, "CAP:DENY:", 9) == 0) {
    die("allow_emitted_deny_marker");
  }
  if (!audit_ring_contains(subject, cap_id, CAP_OK)) {
    die("allow_missing_audit_check_ok");
  }
}

static void exercise_gate_deny(const char *label,
                               device_gate_fn gate,
                               capability_id_t cap_id,
                               cap_subject_id_t subject) {
  char buf[DENY_BUF_BYTES];
  memset(buf, 0, sizeof(buf));

  cap_result_t r = gate(subject, buf, sizeof(buf));
  if (r != CAP_ERR_MISSING) {
    fprintf(stderr, "deny-path label=%s expected CAP_ERR_MISSING got %d\n", label, r);
    die("deny_not_missing");
  }
  /* The buffer must contain a conformant CAP:DENY:<sid>:<cap>:-\n line. */
  char reason[64];
  reason[0] = '\0';
  if (cap_deny_marker_validate(buf, reason, sizeof(reason)) != 0) {
    fprintf(stderr, "deny-path label=%s marker invalid: reason=%s line=%s\n",
            label, reason, buf);
    die("deny_marker_invalid");
  }
  const char *cap_name = cap_deny_marker_name(cap_id);
  if (cap_name == NULL) {
    die("cap_name_lookup_failed");
  }
  /* Spot-check: marker has the right cap name + literal "-" resource. */
  if (strstr(buf, cap_name) == NULL) {
    die("deny_marker_missing_cap_name");
  }
  if (strstr(buf, ":-\n") == NULL) {
    die("deny_marker_missing_dash_resource");
  }
  if (!audit_ring_contains(subject, cap_id, CAP_ERR_MISSING)) {
    die("deny_missing_audit_check_missing");
  }
}

int main(void) {
  printf("TEST:START:win_gfx_gates\n");

  cap_reset_for_tests();

  /* Bootstrap a launcher actor with CAP_CAPABILITY_ADMIN, then grant
   * the three device caps to WIN_APP_SUBJECT but NOT to
   * DENIED_APP_SUBJECT. */
  if (cap_grant_as_for_tests(LAUNCHER_ROOT_SUBJECT,
                             LAUNCHER_SUBJECT,
                             CAP_CAPABILITY_ADMIN) != CAP_OK) {
    die("bootstrap_launcher_admin");
  }
  if (cap_grant_as_for_tests(LAUNCHER_SUBJECT, WIN_APP_SUBJECT,
                             CAP_GFX_FRAMEBUFFER) != CAP_OK) {
    die("grant_gfx");
  }
  if (cap_grant_as_for_tests(LAUNCHER_SUBJECT, WIN_APP_SUBJECT,
                             CAP_INPUT_KEYBOARD) != CAP_OK) {
    die("grant_keyboard");
  }
  if (cap_grant_as_for_tests(LAUNCHER_SUBJECT, WIN_APP_SUBJECT,
                             CAP_INPUT_MOUSE) != CAP_OK) {
    die("grant_mouse");
  }

  /* Allow paths. */
  exercise_gate_allow("gfx_framebuffer", cap_gfx_framebuffer_gate,
                      CAP_GFX_FRAMEBUFFER, WIN_APP_SUBJECT);
  exercise_gate_allow("input_keyboard", cap_input_keyboard_gate,
                      CAP_INPUT_KEYBOARD, WIN_APP_SUBJECT);
  exercise_gate_allow("input_mouse", cap_input_mouse_gate,
                      CAP_INPUT_MOUSE, WIN_APP_SUBJECT);

  /* Deny paths. */
  exercise_gate_deny("gfx_framebuffer", cap_gfx_framebuffer_gate,
                     CAP_GFX_FRAMEBUFFER, DENIED_APP_SUBJECT);
  exercise_gate_deny("input_keyboard", cap_input_keyboard_gate,
                     CAP_INPUT_KEYBOARD, DENIED_APP_SUBJECT);
  exercise_gate_deny("input_mouse", cap_input_mouse_gate,
                     CAP_INPUT_MOUSE, DENIED_APP_SUBJECT);

  /* Defensive: NULL deny buffer must not crash; the gate still records
   * the audit deny event. */
  if (cap_gfx_framebuffer_gate(DENIED_APP_SUBJECT, NULL, 0) !=
      CAP_ERR_MISSING) {
    die("null_buf_deny_not_missing");
  }

  printf("TEST:PASS:win_gfx_framebuffer_gate_allow\n");
  printf("TEST:PASS:win_gfx_framebuffer_gate_deny\n");
  printf("TEST:PASS:win_input_keyboard_gate_allow\n");
  printf("TEST:PASS:win_input_keyboard_gate_deny\n");
  printf("TEST:PASS:win_input_mouse_gate_allow\n");
  printf("TEST:PASS:win_input_mouse_gate_deny\n");
  printf("TEST:PASS:win_gfx_gates\n");
  return 0;
}
