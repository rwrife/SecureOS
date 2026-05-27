/**
 * @file win_gfx_callsite_test.c
 * @brief HAL call-site enforcement test for issue #349.
 *
 * Purpose:
 *   PR #357 landed the gate primitive trio
 *   (cap_gfx_framebuffer_gate / cap_input_keyboard_gate /
 *   cap_input_mouse_gate). This test pins the *call-site* contract on
 *   top of those primitives: the subject-scoped HAL wrappers in
 *   kernel/hal/hal_cap_entry.c (video_hal_write_as,
 *   input_hal_try_read_char_as, mouse_hal_poll_event_as) MUST invoke
 *   the gate *before* delegating to the underlying
 *   backend-neutral primitive (video_hal_write,
 *   input_hal_try_read_char, mouse_hal_poll_event).
 *
 *   Concretely:
 *     1. Allow path: gate returns CAP_OK; wrapper invokes the backend
 *        primitive exactly once; deny marker buffer is left untouched;
 *        audit ring records a CAP_AUDIT_OP_CHECK with result CAP_OK.
 *     2. Deny path: gate returns CAP_ERR_MISSING; wrapper does NOT
 *        invoke the backend primitive (deny-by-default invariant);
 *        deny marker buffer contains a conformant
 *        `CAP:DENY:<sid>:<cap>:-\n` line; audit ring records a
 *        CAP_AUDIT_OP_CHECK with result CAP_ERR_MISSING.
 *
 *   The HAL backend primitives are stubbed below so this test does not
 *   pull in the x86 PS/2 / VGA driver paths; the stubs record their
 *   own invocation count so we can prove the wrapper short-circuits on
 *   deny rather than relying on a downstream backend to refuse.
 *
 * Interactions:
 *   - kernel/hal/hal_cap_entry.c: subject-scoped wrappers under test.
 *   - kernel/cap/cap_gate.c: gate primitive trio (#357).
 *   - kernel/cap/cap_deny_marker.c: marker validator / cap name lookup.
 *   - kernel/cap/capability.c, cap_table.c: audit ring + grant/check.
 *
 * Launched by:
 *   build/scripts/test_win_gfx_callsite.sh, dispatched via test.sh.
 *
 * Issue: #349 (call-site wiring follow-up to PR #357).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_deny_marker.h"
#include "../kernel/cap/cap_gate.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/hal/hal_cap_entry.h"
#include "../kernel/hal/mouse_hal.h"

#define DENY_BUF_BYTES 128u

static const cap_subject_id_t LAUNCHER_ROOT_SUBJECT = 0u;
static const cap_subject_id_t LAUNCHER_SUBJECT = 1u;
static const cap_subject_id_t WIN_APP_SUBJECT = 3u;
static const cap_subject_id_t DENIED_APP_SUBJECT = 4u;

/* ----------------------------------------------------------------
 * HAL backend stubs. The wrappers under test delegate to these on the
 * allow path; on the deny path the gate must short-circuit and they
 * must NOT be invoked. We count invocations to prove the contract.
 * --------------------------------------------------------------*/
static int g_video_write_calls;
static int g_input_read_calls;
static int g_mouse_poll_calls;
static char g_last_video_message[64];
static int g_input_read_yields_char; /* 0 = no data, 1 = yield 'X' */
static int g_mouse_poll_yields_event; /* 0 = no event, 1 = yield event */

void video_hal_write(const char *message) {
  g_video_write_calls++;
  if (message != NULL) {
    size_t len = strlen(message);
    if (len >= sizeof(g_last_video_message)) {
      len = sizeof(g_last_video_message) - 1u;
    }
    memcpy(g_last_video_message, message, len);
    g_last_video_message[len] = '\0';
  }
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

static void die(const char *reason) {
  printf("TEST:FAIL:win_gfx_callsite:%s\n", reason);
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

static void assert_deny_marker(const char *label,
                               const char *buf,
                               capability_id_t cap_id) {
  char reason[64];
  reason[0] = '\0';
  if (cap_deny_marker_validate(buf, reason, sizeof(reason)) != 0) {
    fprintf(stderr,
            "deny-path label=%s marker invalid: reason=%s line=%s\n",
            label, reason, buf);
    die("deny_marker_invalid");
  }
  const char *cap_name = cap_deny_marker_name(cap_id);
  if (cap_name == NULL || strstr(buf, cap_name) == NULL) {
    die("deny_marker_missing_cap_name");
  }
  if (strstr(buf, ":-\n") == NULL) {
    die("deny_marker_missing_dash_resource");
  }
}

static void test_video_callsite_allow(void) {
  char buf[DENY_BUF_BYTES];
  memset(buf, 0x7F, sizeof(buf));

  int before = g_video_write_calls;
  cap_result_t r = video_hal_write_as(WIN_APP_SUBJECT,
                                      "secureos-fb-banner",
                                      buf, sizeof(buf));
  if (r != CAP_OK) {
    die("video_allow_not_ok");
  }
  if (g_video_write_calls != before + 1) {
    die("video_allow_backend_not_called");
  }
  if (memcmp(buf, "CAP:DENY:", 9) == 0) {
    die("video_allow_emitted_deny_marker");
  }
  if (strcmp(g_last_video_message, "secureos-fb-banner") != 0) {
    die("video_allow_payload_corrupted");
  }
  if (!audit_ring_contains(WIN_APP_SUBJECT, CAP_GFX_FRAMEBUFFER, CAP_OK)) {
    die("video_allow_missing_audit_check_ok");
  }
}

static void test_video_callsite_deny(void) {
  char buf[DENY_BUF_BYTES];
  memset(buf, 0, sizeof(buf));

  int before = g_video_write_calls;
  cap_result_t r = video_hal_write_as(DENIED_APP_SUBJECT,
                                      "rogue-fb-banner",
                                      buf, sizeof(buf));
  if (r != CAP_ERR_MISSING) {
    die("video_deny_not_missing");
  }
  if (g_video_write_calls != before) {
    die("video_deny_backend_invoked");
  }
  assert_deny_marker("video", buf, CAP_GFX_FRAMEBUFFER);
  if (!audit_ring_contains(DENIED_APP_SUBJECT, CAP_GFX_FRAMEBUFFER,
                           CAP_ERR_MISSING)) {
    die("video_deny_missing_audit_check_missing");
  }
}

static void test_input_callsite_allow(void) {
  char buf[DENY_BUF_BYTES];
  memset(buf, 0x7F, sizeof(buf));

  /* Backing input HAL has a byte waiting. */
  g_input_read_yields_char = 1;
  int before = g_input_read_calls;
  char ch = '\0';
  int avail = -1;
  cap_result_t r = input_hal_try_read_char_as(WIN_APP_SUBJECT,
                                              &ch, &avail,
                                              buf, sizeof(buf));
  if (r != CAP_OK) {
    die("input_allow_not_ok");
  }
  if (g_input_read_calls != before + 1) {
    die("input_allow_backend_not_called");
  }
  if (avail != 1 || ch != 'X') {
    die("input_allow_no_char_yielded");
  }
  if (memcmp(buf, "CAP:DENY:", 9) == 0) {
    die("input_allow_emitted_deny_marker");
  }
  if (!audit_ring_contains(WIN_APP_SUBJECT, CAP_INPUT_KEYBOARD, CAP_OK)) {
    die("input_allow_missing_audit_check_ok");
  }

  /* Allow with no pending byte: avail must be 0 and r must still be CAP_OK. */
  g_input_read_yields_char = 0;
  before = g_input_read_calls;
  ch = '\0';
  avail = -1;
  r = input_hal_try_read_char_as(WIN_APP_SUBJECT, &ch, &avail,
                                 NULL, 0);
  if (r != CAP_OK) {
    die("input_allow_empty_not_ok");
  }
  if (g_input_read_calls != before + 1) {
    die("input_allow_empty_backend_not_called");
  }
  if (avail != 0) {
    die("input_allow_empty_avail_not_zero");
  }
}

static void test_input_callsite_deny(void) {
  char buf[DENY_BUF_BYTES];
  memset(buf, 0, sizeof(buf));

  int before = g_input_read_calls;
  char ch = 0;
  int avail = -1;
  cap_result_t r = input_hal_try_read_char_as(DENIED_APP_SUBJECT,
                                              &ch, &avail,
                                              buf, sizeof(buf));
  if (r != CAP_ERR_MISSING) {
    die("input_deny_not_missing");
  }
  if (g_input_read_calls != before) {
    die("input_deny_backend_invoked");
  }
  if (ch != 0) {
    die("input_deny_clobbered_out_char");
  }
  if (avail != -1) {
    die("input_deny_clobbered_avail");
  }
  assert_deny_marker("input", buf, CAP_INPUT_KEYBOARD);
  if (!audit_ring_contains(DENIED_APP_SUBJECT, CAP_INPUT_KEYBOARD,
                           CAP_ERR_MISSING)) {
    die("input_deny_missing_audit_check_missing");
  }
}

static void test_mouse_callsite_allow(void) {
  char buf[DENY_BUF_BYTES];
  memset(buf, 0x7F, sizeof(buf));

  g_mouse_poll_yields_event = 1;
  int before = g_mouse_poll_calls;
  mouse_event_t evt = {0};
  int avail = -1;
  cap_result_t r = mouse_hal_poll_event_as(WIN_APP_SUBJECT,
                                           &evt, &avail,
                                           buf, sizeof(buf));
  if (r != CAP_OK) {
    die("mouse_allow_not_ok");
  }
  if (g_mouse_poll_calls != before + 1) {
    die("mouse_allow_backend_not_called");
  }
  if (avail != 1 || evt.x != 7 || evt.y != 11 ||
      evt.type != MOUSE_EVENT_MOVE) {
    die("mouse_allow_event_not_delivered");
  }
  if (memcmp(buf, "CAP:DENY:", 9) == 0) {
    die("mouse_allow_emitted_deny_marker");
  }
  if (!audit_ring_contains(WIN_APP_SUBJECT, CAP_INPUT_MOUSE, CAP_OK)) {
    die("mouse_allow_missing_audit_check_ok");
  }
}

static void test_mouse_callsite_deny(void) {
  char buf[DENY_BUF_BYTES];
  memset(buf, 0, sizeof(buf));

  int before = g_mouse_poll_calls;
  mouse_event_t evt = {0};
  evt.x = 42;
  evt.y = 99;
  int avail = -1;
  cap_result_t r = mouse_hal_poll_event_as(DENIED_APP_SUBJECT,
                                           &evt, &avail,
                                           buf, sizeof(buf));
  if (r != CAP_ERR_MISSING) {
    die("mouse_deny_not_missing");
  }
  if (g_mouse_poll_calls != before) {
    die("mouse_deny_backend_invoked");
  }
  if (evt.x != 42 || evt.y != 99) {
    die("mouse_deny_clobbered_out_event");
  }
  if (avail != -1) {
    die("mouse_deny_clobbered_avail");
  }
  assert_deny_marker("mouse", buf, CAP_INPUT_MOUSE);
  if (!audit_ring_contains(DENIED_APP_SUBJECT, CAP_INPUT_MOUSE,
                           CAP_ERR_MISSING)) {
    die("mouse_deny_missing_audit_check_missing");
  }
}

static void test_null_marker_buf_still_denies(void) {
  /* Defensive: NULL deny buffer must not crash the wrapper; the gate
   * still records the deny event and the backend MUST NOT be invoked. */
  int video_before = g_video_write_calls;
  int input_before = g_input_read_calls;
  int mouse_before = g_mouse_poll_calls;

  if (video_hal_write_as(DENIED_APP_SUBJECT, "x", NULL, 0) !=
      CAP_ERR_MISSING) {
    die("null_buf_video_deny_not_missing");
  }
  if (g_video_write_calls != video_before) {
    die("null_buf_video_backend_invoked_on_deny");
  }

  char ch = 0;
  int avail = -1;
  if (input_hal_try_read_char_as(DENIED_APP_SUBJECT, &ch, &avail,
                                 NULL, 0) != CAP_ERR_MISSING) {
    die("null_buf_input_deny_not_missing");
  }
  if (g_input_read_calls != input_before) {
    die("null_buf_input_backend_invoked_on_deny");
  }

  mouse_event_t evt = {0};
  if (mouse_hal_poll_event_as(DENIED_APP_SUBJECT, &evt, &avail,
                              NULL, 0) != CAP_ERR_MISSING) {
    die("null_buf_mouse_deny_not_missing");
  }
  if (g_mouse_poll_calls != mouse_before) {
    die("null_buf_mouse_backend_invoked_on_deny");
  }
}

int main(void) {
  printf("TEST:START:win_gfx_callsite\n");

  cap_reset_for_tests();

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

  test_video_callsite_allow();
  test_video_callsite_deny();
  test_input_callsite_allow();
  test_input_callsite_deny();
  test_mouse_callsite_allow();
  test_mouse_callsite_deny();
  test_null_marker_buf_still_denies();

  printf("TEST:PASS:win_gfx_framebuffer_callsite_allow\n");
  printf("TEST:PASS:win_gfx_framebuffer_callsite_deny\n");
  printf("TEST:PASS:win_input_keyboard_callsite_allow\n");
  printf("TEST:PASS:win_input_keyboard_callsite_deny\n");
  printf("TEST:PASS:win_input_mouse_callsite_allow\n");
  printf("TEST:PASS:win_input_mouse_callsite_deny\n");
  printf("TEST:PASS:win_gfx_callsite\n");
  return 0;
}
