/**
 * @file win_gfx_hal_deny_qemu_test.c
 * @brief Deny-path substrate peer for the HAL call-site gates
 *        (issue #376, follow-up to #349 / PR #365).
 *
 * Counterpart to win_gfx_hal_allow_qemu_test.c. Spawns a real PCB via
 * the M2 substrate launcher path with NO GFX/INPUT cap grants, then
 * drives each of the three subject-scoped HAL wrappers and asserts:
 *
 *   - each wrapper returns CAP_ERR_MISSING,
 *   - the underlying backend primitive (video_hal_write,
 *     input_hal_try_read_char, mouse_hal_poll_event) is NOT invoked
 *     (deny-by-default invariant),
 *   - the deny_marker_buf populated by each wrapper passes
 *     cap_deny_marker_validate() per
 *     `docs/abi/capability-deny-contract.md` \u00a74.3,
 *   - the audit ring records CAP_AUDIT_OP_CHECK with
 *     result = CAP_ERR_MISSING for each of the three caps.
 *
 * Output markers (consumed by build/scripts/test_win_gfx_hal_deny_qemu.sh):
 *   TEST:PASS:win_gfx_hal_deny_qemu:video_deny_backend_not_called
 *   TEST:PASS:win_gfx_hal_deny_qemu:input_deny_backend_not_called
 *   TEST:PASS:win_gfx_hal_deny_qemu:mouse_deny_backend_not_called
 *   TEST:PASS:win_gfx_hal_deny_qemu:deny_marker_conformant
 *   TEST:PASS:win_gfx_hal_deny_qemu:audit_check_missing_recorded
 *   TEST:PASS:win_gfx_hal_deny_qemu
 *
 * Issue: #376. Plan: BUILD_ROADMAP.md §5.5 "Validate" / §5.6.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_deny_marker.h"
#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/hal/hal_cap_entry.h"
#include "../kernel/hal/mouse_hal.h"
#include "../kernel/proc/process.h"
#include "../kernel/user/launcher.h"
#include "harness/m2_subjects.h"

#define DENY_BUF_BYTES 128u

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:win_gfx_hal_deny_qemu:%s\n", reason);
  g_fail = 1;
}

/* HAL backend stubs — invocation counters must stay zero on the deny
 * path; the wrapper short-circuits before any delegate call. */
static int g_video_write_calls;
static int g_input_read_calls;
static int g_mouse_poll_calls;

void video_hal_write(const char *message) {
  (void)message;
  g_video_write_calls++;
}

int input_hal_try_read_char(char *out_char) {
  g_input_read_calls++;
  if (out_char != NULL) {
    *out_char = 'X';
  }
  return 1;
}

int mouse_hal_poll_event(mouse_event_t *out_event) {
  g_mouse_poll_calls++;
  if (out_event != NULL) {
    out_event->type = MOUSE_EVENT_MOVE;
    out_event->x = 1;
    out_event->y = 2;
    out_event->button = 0;
  }
  return 1;
}

static void reset_world(void) {
  launcher_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  launcher_spawn_reset();
}

static int audit_ring_has_check_missing(cap_subject_id_t subject_id,
                                        capability_id_t capability_id) {
  size_t count = cap_audit_count_for_tests();
  for (size_t i = 0u; i < count; ++i) {
    cap_audit_event_t event = {0};
    if (cap_audit_get_for_tests(i, &event) != CAP_OK) {
      continue;
    }
    if (event.operation == CAP_AUDIT_OP_CHECK &&
        event.subject_id == subject_id &&
        event.capability_id == capability_id &&
        event.result == CAP_ERR_MISSING) {
      return 1;
    }
  }
  return 0;
}

static int marker_conformant(const char *buf, capability_id_t cap_id) {
  char reason[64];
  reason[0] = '\0';
  if (cap_deny_marker_validate(buf, reason, sizeof(reason)) != 0) {
    fprintf(stderr, "deny marker invalid: reason=%s line=%s\n",
            reason, buf);
    return 0;
  }
  const char *cap_name = cap_deny_marker_name(cap_id);
  if (cap_name == NULL || strstr(buf, cap_name) == NULL) {
    fprintf(stderr, "deny marker missing cap name %s in %s\n",
            cap_name ? cap_name : "<null>", buf);
    return 0;
  }
  if (strstr(buf, ":-\n") == NULL) {
    fprintf(stderr, "deny marker missing trailing :-\\n in %s\n", buf);
    return 0;
  }
  return 1;
}

int main(void) {
  reset_world();

  /* Spawn a real PCB via the M2 substrate launcher path. Empty
   * auto_grant list: no GFX/INPUT caps are minted, so every wrapper
   * call must take the deny path. */
  launcher_manifest_t m = {
      .subject_id       = (cap_subject_id_t)SUBJECT_M2_HELLOAPP,
      .auto_grant_caps  = NULL,
      .auto_grant_count = 0u,
  };
  launcher_spawn_t sp;
  if (launcher_spawn_app_from_manifest(&m, &sp) != LAUNCHER_OK) {
    fail("launcher_spawn_failed");
    goto out;
  }
  if (sp.pid == PID_INVALID || !process_is_live_for_tests(sp.pid)) {
    fail("spawned_pcb_not_live");
    goto out;
  }

  const cap_subject_id_t subj = (cap_subject_id_t)SUBJECT_M2_HELLOAPP;

  /* Video deny. */
  char vbuf[DENY_BUF_BYTES];
  memset(vbuf, 0, sizeof(vbuf));
  int v_before = g_video_write_calls;
  cap_result_t vr = video_hal_write_as(subj, "rogue-fb-qemu",
                                       vbuf, sizeof(vbuf));
  if (vr != CAP_ERR_MISSING) {
    fail("video_deny_not_missing");
    goto out;
  }
  if (g_video_write_calls != v_before) {
    fail("video_deny_backend_invoked");
    goto out;
  }
  printf("TEST:PASS:win_gfx_hal_deny_qemu:"
         "video_deny_backend_not_called\n");

  /* Input deny. */
  char ibuf[DENY_BUF_BYTES];
  memset(ibuf, 0, sizeof(ibuf));
  int i_before = g_input_read_calls;
  char ch = 0;
  int kb_avail = -1;
  cap_result_t ir = input_hal_try_read_char_as(subj, &ch, &kb_avail,
                                               ibuf, sizeof(ibuf));
  if (ir != CAP_ERR_MISSING) {
    fail("input_deny_not_missing");
    goto out;
  }
  if (g_input_read_calls != i_before) {
    fail("input_deny_backend_invoked");
    goto out;
  }
  if (ch != 0 || kb_avail != -1) {
    fail("input_deny_clobbered_out_params");
    goto out;
  }
  printf("TEST:PASS:win_gfx_hal_deny_qemu:"
         "input_deny_backend_not_called\n");

  /* Mouse deny. */
  char mbuf[DENY_BUF_BYTES];
  memset(mbuf, 0, sizeof(mbuf));
  int m_before = g_mouse_poll_calls;
  mouse_event_t evt = {0};
  evt.x = 42;
  evt.y = 99;
  int mouse_avail = -1;
  cap_result_t mr = mouse_hal_poll_event_as(subj, &evt, &mouse_avail,
                                            mbuf, sizeof(mbuf));
  if (mr != CAP_ERR_MISSING) {
    fail("mouse_deny_not_missing");
    goto out;
  }
  if (g_mouse_poll_calls != m_before) {
    fail("mouse_deny_backend_invoked");
    goto out;
  }
  if (evt.x != 42 || evt.y != 99 || mouse_avail != -1) {
    fail("mouse_deny_clobbered_out_params");
    goto out;
  }
  printf("TEST:PASS:win_gfx_hal_deny_qemu:"
         "mouse_deny_backend_not_called\n");

  /* All three deny markers must pass cap_deny_marker_validate and
   * carry the corresponding cap name. */
  if (!marker_conformant(vbuf, CAP_GFX_FRAMEBUFFER) ||
      !marker_conformant(ibuf, CAP_INPUT_KEYBOARD) ||
      !marker_conformant(mbuf, CAP_INPUT_MOUSE)) {
    fail("deny_marker_invalid");
    goto out;
  }
  printf("TEST:PASS:win_gfx_hal_deny_qemu:deny_marker_conformant\n");

  /* Audit ring: one CAP_AUDIT_OP_CHECK per cap, result = CAP_ERR_MISSING. */
  if (!audit_ring_has_check_missing(subj, CAP_GFX_FRAMEBUFFER)) {
    fail("audit_check_missing_video_absent");
    goto out;
  }
  if (!audit_ring_has_check_missing(subj, CAP_INPUT_KEYBOARD)) {
    fail("audit_check_missing_input_absent");
    goto out;
  }
  if (!audit_ring_has_check_missing(subj, CAP_INPUT_MOUSE)) {
    fail("audit_check_missing_mouse_absent");
    goto out;
  }
  printf("TEST:PASS:win_gfx_hal_deny_qemu:audit_check_missing_recorded\n");

  if (launcher_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("launcher_spawn_destroy_failed");
    goto out;
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:win_gfx_hal_deny_qemu\n");
  return 0;
}
