/**
 * @file win_gfx_hal_allow_qemu_test.c
 * @brief Allow-path substrate peer for the HAL call-site gates
 *        (issue #376, follow-up to #349 / PR #365).
 *
 * Rides on the merged M1/M2 substrate plus the cap_gate primitive
 * trio (#357) and the subject-scoped HAL wrappers (#349 / PR #365).
 *
 *   1. `launcher_spawn_app_from_manifest()` spawns a real PCB owned
 *      by SUBJECT_M2_HELLOAPP. The launcher's v0 auto_grant path only
 *      knows how to hand off CAP_CONSOLE_WRITE in the scratch slot;
 *      the GFX/INPUT caps are minted post-spawn via cap_table_grant
 *      against the same subject id the launcher created (mirrors the
 *      shape of `tests/win_gfx_callsite_test.c`, but on a real
 *      live PCB rather than a synthetic subject). This is the
 *      `_qemu` tier discriminator per BUILD_ROADMAP §5.5/§5.6.
 *   2. Drives each of the three subject-scoped HAL wrappers exactly
 *      once with the live PCB's subject id, asserting:
 *        - return value is CAP_OK,
 *        - the underlying backend primitive (video_hal_write /
 *          input_hal_try_read_char / mouse_hal_poll_event, stubbed
 *          below) was invoked exactly once,
 *        - the audit ring contains a CAP_AUDIT_OP_CHECK with
 *          result = CAP_OK for the corresponding capability id.
 *   3. Tears the spawn down via launcher_spawn_destroy.
 *
 * The HAL backend primitives are stubbed at link time (same pattern as
 * tests/win_gfx_callsite_test.c) so this peer does not pull in the
 * x86 PS/2 / VGA driver paths.
 *
 * Output markers (consumed by build/scripts/test_win_gfx_hal_allow_qemu.sh):
 *   TEST:PASS:win_gfx_hal_allow_qemu:video_allow_backend_called_once
 *   TEST:PASS:win_gfx_hal_allow_qemu:input_allow_backend_called_once
 *   TEST:PASS:win_gfx_hal_allow_qemu:mouse_allow_backend_called_once
 *   TEST:PASS:win_gfx_hal_allow_qemu:audit_check_ok_recorded
 *   TEST:PASS:win_gfx_hal_allow_qemu
 *
 * Issue: #376. Plan: BUILD_ROADMAP.md §5.5 "Validate" / §5.6.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  printf("TEST:FAIL:win_gfx_hal_allow_qemu:%s\n", reason);
  g_fail = 1;
}

/* ----------------------------------------------------------------
 * HAL backend stubs (mirror tests/win_gfx_callsite_test.c). The
 * wrappers under test delegate to these on allow; we count invocations
 * to prove the call-site contract.
 * --------------------------------------------------------------*/
static int g_video_write_calls;
static int g_input_read_calls;
static int g_mouse_poll_calls;
static int g_input_read_yields_char;
static int g_mouse_poll_yields_event;

void video_hal_write(const char *message) {
  (void)message;
  g_video_write_calls++;
}

int input_hal_try_read_char(char *out_char) {
  g_input_read_calls++;
  if (g_input_read_yields_char && out_char != NULL) {
    *out_char = 'X';
    return 1;
  }
  return 0;
}

int mouse_hal_poll_event(mouse_event_t *out_event) {
  g_mouse_poll_calls++;
  if (g_mouse_poll_yields_event && out_event != NULL) {
    out_event->type = MOUSE_EVENT_MOVE;
    out_event->x = 7;
    out_event->y = 11;
    out_event->button = 0;
    return 1;
  }
  return 0;
}

static void reset_world(void) {
  launcher_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  launcher_spawn_reset();
}

static int audit_ring_has_check(cap_subject_id_t subject_id,
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

int main(void) {
  reset_world();

  /* Spawn a real PCB via the M2 substrate launcher path. v0 manifest
   * auto_grant only supports CAP_CONSOLE_WRITE; the GFX/INPUT caps for
   * the wrapper allow path are minted post-spawn via cap_table_grant
   * against the launched subject id. */
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
  if (cap_table_grant(subj, CAP_GFX_FRAMEBUFFER) != CAP_OK ||
      cap_table_grant(subj, CAP_INPUT_KEYBOARD)  != CAP_OK ||
      cap_table_grant(subj, CAP_INPUT_MOUSE)     != CAP_OK) {
    fail("post_spawn_cap_grant_failed");
    goto out;
  }

  /* Video allow: wrapper must invoke video_hal_write exactly once. */
  char vbuf[DENY_BUF_BYTES];
  memset(vbuf, 0x7F, sizeof(vbuf));
  int v_before = g_video_write_calls;
  if (video_hal_write_as(subj, "secureos-fb-qemu",
                         vbuf, sizeof(vbuf)) != CAP_OK) {
    fail("video_allow_not_ok");
    goto out;
  }
  if (g_video_write_calls != v_before + 1) {
    fail("video_allow_backend_call_count_wrong");
    goto out;
  }
  if (memcmp(vbuf, "CAP:DENY:", 9) == 0) {
    fail("video_allow_emitted_deny_marker");
    goto out;
  }
  printf("TEST:PASS:win_gfx_hal_allow_qemu:"
         "video_allow_backend_called_once\n");

  /* Input allow: wrapper must invoke input_hal_try_read_char once. */
  char ibuf[DENY_BUF_BYTES];
  memset(ibuf, 0x7F, sizeof(ibuf));
  g_input_read_yields_char = 1;
  int i_before = g_input_read_calls;
  char ch = '\0';
  int kb_avail = -1;
  if (input_hal_try_read_char_as(subj, &ch, &kb_avail,
                                 ibuf, sizeof(ibuf)) != CAP_OK) {
    fail("input_allow_not_ok");
    goto out;
  }
  if (g_input_read_calls != i_before + 1) {
    fail("input_allow_backend_call_count_wrong");
    goto out;
  }
  if (kb_avail != 1 || ch != 'X') {
    fail("input_allow_byte_not_delivered");
    goto out;
  }
  if (memcmp(ibuf, "CAP:DENY:", 9) == 0) {
    fail("input_allow_emitted_deny_marker");
    goto out;
  }
  printf("TEST:PASS:win_gfx_hal_allow_qemu:"
         "input_allow_backend_called_once\n");

  /* Mouse allow: wrapper must invoke mouse_hal_poll_event once. */
  char mbuf[DENY_BUF_BYTES];
  memset(mbuf, 0x7F, sizeof(mbuf));
  g_mouse_poll_yields_event = 1;
  int m_before = g_mouse_poll_calls;
  mouse_event_t evt = {0};
  int mouse_avail = -1;
  if (mouse_hal_poll_event_as(subj, &evt, &mouse_avail,
                              mbuf, sizeof(mbuf)) != CAP_OK) {
    fail("mouse_allow_not_ok");
    goto out;
  }
  if (g_mouse_poll_calls != m_before + 1) {
    fail("mouse_allow_backend_call_count_wrong");
    goto out;
  }
  if (mouse_avail != 1 || evt.x != 7 || evt.y != 11 ||
      evt.type != MOUSE_EVENT_MOVE) {
    fail("mouse_allow_event_not_delivered");
    goto out;
  }
  if (memcmp(mbuf, "CAP:DENY:", 9) == 0) {
    fail("mouse_allow_emitted_deny_marker");
    goto out;
  }
  printf("TEST:PASS:win_gfx_hal_allow_qemu:"
         "mouse_allow_backend_called_once\n");

  /* Audit ring: one CAP_AUDIT_OP_CHECK per cap, result = CAP_OK. */
  if (!audit_ring_has_check(subj, CAP_GFX_FRAMEBUFFER, CAP_OK)) {
    fail("audit_check_ok_video_missing");
    goto out;
  }
  if (!audit_ring_has_check(subj, CAP_INPUT_KEYBOARD, CAP_OK)) {
    fail("audit_check_ok_input_missing");
    goto out;
  }
  if (!audit_ring_has_check(subj, CAP_INPUT_MOUSE, CAP_OK)) {
    fail("audit_check_ok_mouse_missing");
    goto out;
  }
  printf("TEST:PASS:win_gfx_hal_allow_qemu:audit_check_ok_recorded\n");

  if (launcher_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("launcher_spawn_destroy_failed");
    goto out;
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:win_gfx_hal_allow_qemu\n");
  return 0;
}
