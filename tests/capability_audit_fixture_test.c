/**
 * @file capability_audit_fixture_test.c
 * @brief Byte-exact fixture-diff regression test for the M2 console
 *        capability-audit line set (M1-CAPTBL-005, plan #197).
 *
 * Purpose:
 *   Pins the exact `CAP_AUDIT:` line bytes emitted by a representative
 *   M2 console capability sequence. The M1 kernel capability table
 *   skeleton (#225/#231) and the 32-bit handle layer (#237) split the
 *   capability subsystem into a row/handle store plus a thin legacy
 *   façade. Plan acceptance #2 requires that the audit byte-stream stay
 *   bit-identical through that migration — this file is the guard.
 *
 *   The fixture is intentionally legacy-API-only (cap_console_write_gate
 *   + cap_grant_for_tests + cap_revoke_for_tests) so that any future
 *   refactor of `cap_table.{c,h}` onto `cap_handle.{c,h}` (M1-CAPTBL-005
 *   façade migration) must keep these lines byte-identical or fail this
 *   test loudly with a per-line diff.
 *
 *   Coverage chosen to be representative without explosion:
 *     - GRANT OK / REVOKE OK / CHECK OK (ALLOW) / CHECK MISSING (DENY)
 *     - Invalid-subject GRANT/REVOKE/CHECK paths (the M2 launcher rejects
 *       these upstream today, but the audit layer would emit DENY-class
 *       outcomes if a future refactor ever bypassed the launcher guard).
 *     - Invalid-cap-id GRANT path (same reasoning).
 *     - Two distinct capability ids (CAP_CONSOLE_WRITE, CAP_SERIAL_WRITE)
 *       so a typo in cap-id formatting would surface here, not just in
 *       the cap=1-only audit_log test.
 *
 *   On mismatch the test prints, for each diverging line:
 *     TEST:FAIL:capability_audit_fixture:line_mismatch:idx=N:got=...:want=...
 *   so CI logs are self-diagnostic without needing the .log artefact.
 *
 * Interactions:
 *   - kernel/cap/capability.{h,c}: audit ring + formatter under test.
 *   - kernel/cap/cap_table.{c,h}: capability decisions feeding the ring.
 *   - kernel/cap/cap_gate.c: cap_console_write_gate is the M2 entry point.
 *
 * Launched by:
 *   build/scripts/test_capability_audit_fixture.sh, wired into
 *   build/scripts/test.sh as the `capability_audit_fixture` target.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/capability.h"
#include "../kernel/cap/cap_gate.h"
#include "../kernel/cap/cap_table.h"

static int fail_count;

static void fail(const char *reason) {
  printf("TEST:FAIL:capability_audit_fixture:%s\n", reason);
  fail_count++;
}

/*
 * Frozen byte-exact fixture for the M2 console + serial sequence.
 *
 * If a refactor of the capability subsystem changes any of these bytes
 * (e.g. cap=1 -> cap=01, or a re-ordering of GRANT vs CHECK), the test
 * MUST fail. Updating this fixture is allowed only when the audit ABI is
 * explicitly bumped per BUILD_ROADMAP §7 / docs/abi/capabilities.md.
 *
 * Sequence (one line per emitted audit event):
 *   0: cap_grant_for_tests(app_a=2, CAP_CONSOLE_WRITE)
 *      -> GRANT OK / GRANTED
 *   1: cap_console_write_gate(app_a=2, "hi", ...)
 *      -> CHECK OK / ALLOW
 *   2: cap_console_write_gate(app_b=3, "no", ...)
 *      -> CHECK MISSING / DENY  (app_b never granted)
 *   3: cap_revoke_for_tests(app_a=2, CAP_CONSOLE_WRITE)
 *      -> REVOKE OK / REVOKED
 *   4: cap_console_write_gate(app_a=2, "x", ...)
 *      -> CHECK MISSING / DENY  (post-revoke deny on same subject)
 *   5: cap_grant_for_tests(app_a=2, CAP_SERIAL_WRITE)
 *      -> GRANT OK / GRANTED    (different cap-id, same actor)
 *   6: cap_grant_for_tests(app_a=2, CAP_CONSOLE_WRITE)
 *      -> GRANT OK / GRANTED    (re-grant after revoke is idempotent OK)
 *   7: cap_grant_for_tests(CAP_TABLE_MAX_SUBJECTS, CAP_CONSOLE_WRITE)
 *      -> GRANT SUBJECT_INVALID / GRANT_DENIED
 *   8: cap_revoke_for_tests(CAP_TABLE_MAX_SUBJECTS, CAP_CONSOLE_WRITE)
 *      -> REVOKE SUBJECT_INVALID / REVOKE_DENIED
 *   9: cap_check(CAP_TABLE_MAX_SUBJECTS, CAP_CONSOLE_WRITE)
 *      -> CHECK SUBJECT_INVALID / DENY
 *  10: cap_grant_for_tests(app_a=2, 9999 -- invalid cap id)
 *      -> GRANT CAP_INVALID / GRANT_DENIED
 */
static const char *kFixtureLines[] = {
  "CAP_AUDIT:seq=0:op=GRANT:actor=2:subject=2:cap=1:result=OK:outcome=GRANTED",
  "CAP_AUDIT:seq=1:op=CHECK:actor=2:subject=2:cap=1:result=OK:outcome=ALLOW",
  "CAP_AUDIT:seq=2:op=CHECK:actor=3:subject=3:cap=1:result=MISSING:outcome=DENY",
  "CAP_AUDIT:seq=3:op=REVOKE:actor=2:subject=2:cap=1:result=OK:outcome=REVOKED",
  "CAP_AUDIT:seq=4:op=CHECK:actor=2:subject=2:cap=1:result=MISSING:outcome=DENY",
  "CAP_AUDIT:seq=5:op=GRANT:actor=2:subject=2:cap=2:result=OK:outcome=GRANTED",
  "CAP_AUDIT:seq=6:op=GRANT:actor=2:subject=2:cap=1:result=OK:outcome=GRANTED",
  "CAP_AUDIT:seq=7:op=GRANT:actor=8:subject=8:cap=1:result=SUBJECT_INVALID:outcome=GRANT_DENIED",
  "CAP_AUDIT:seq=8:op=REVOKE:actor=8:subject=8:cap=1:result=SUBJECT_INVALID:outcome=REVOKE_DENIED",
  "CAP_AUDIT:seq=9:op=CHECK:actor=8:subject=8:cap=1:result=SUBJECT_INVALID:outcome=DENY",
  "CAP_AUDIT:seq=10:op=GRANT:actor=2:subject=2:cap=9999:result=CAP_INVALID:outcome=GRANT_DENIED",
};

static const size_t kFixtureLineCount =
    sizeof(kFixtureLines) / sizeof(kFixtureLines[0]);

/* CAP_TABLE_MAX_SUBJECTS is the first out-of-range subject id. Pin it to a
 * literal in the fixture (above) so a future bump of the max would also
 * trip this test and force an audit-ABI review. */
#if CAP_TABLE_MAX_SUBJECTS != 8u
#error "Fixture assumes CAP_TABLE_MAX_SUBJECTS == 8; update fixture lines if bumped"
#endif

static void drive_fixture_sequence(void) {
  const cap_subject_id_t app_a = 2u;
  const cap_subject_id_t app_b = 3u;
  size_t bytes_written = 0u;

  /* seq=0 */
  if (cap_grant_for_tests(app_a, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("grant_app_a_console_failed");
  }
  /* seq=1 */
  if (cap_console_write_gate(app_a, "hi", &bytes_written) != CAP_OK) {
    fail("console_write_app_a_allow_failed");
  }
  /* seq=2 */
  if (cap_console_write_gate(app_b, "no", &bytes_written) != CAP_ERR_MISSING) {
    fail("console_write_app_b_should_deny");
  }
  /* seq=3 */
  if (cap_revoke_for_tests(app_a, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("revoke_app_a_console_failed");
  }
  /* seq=4 */
  if (cap_console_write_gate(app_a, "x", &bytes_written) != CAP_ERR_MISSING) {
    fail("console_write_app_a_post_revoke_should_deny");
  }
  /* seq=5 */
  if (cap_grant_for_tests(app_a, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("grant_app_a_serial_failed");
  }
  /* seq=6 */
  if (cap_grant_for_tests(app_a, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("regrant_app_a_console_failed");
  }
  /* seq=7 */
  if (cap_grant_for_tests((cap_subject_id_t)CAP_TABLE_MAX_SUBJECTS,
                          CAP_CONSOLE_WRITE) != CAP_ERR_SUBJECT_INVALID) {
    fail("grant_invalid_subject_should_reject");
  }
  /* seq=8 */
  if (cap_revoke_for_tests((cap_subject_id_t)CAP_TABLE_MAX_SUBJECTS,
                           CAP_CONSOLE_WRITE) != CAP_ERR_SUBJECT_INVALID) {
    fail("revoke_invalid_subject_should_reject");
  }
  /* seq=9 */
  if (cap_check((cap_subject_id_t)CAP_TABLE_MAX_SUBJECTS,
                CAP_CONSOLE_WRITE) != CAP_ERR_SUBJECT_INVALID) {
    fail("check_invalid_subject_should_reject");
  }
  /* seq=10 */
  if (cap_grant_for_tests(app_a, (capability_id_t)9999) != CAP_ERR_CAP_INVALID) {
    fail("grant_invalid_cap_id_should_reject");
  }
}

static void test_fixture_bytes_identical(void) {
  cap_reset_for_tests();
  drive_fixture_sequence();

  const size_t emitted = cap_audit_count_for_tests();
  if (emitted != kFixtureLineCount) {
    printf("TEST:FAIL:capability_audit_fixture:event_count:got=%zu:want=%zu\n",
           emitted, kFixtureLineCount);
    fail_count++;
    return;
  }

  if (cap_audit_dropped_for_tests() != 0u) {
    fail("dropped_events_nonzero");
  }

  for (size_t i = 0; i < kFixtureLineCount; ++i) {
    cap_audit_event_t event = {0};
    if (cap_audit_get_for_tests(i, &event) != CAP_OK) {
      printf("TEST:FAIL:capability_audit_fixture:missing_event:idx=%zu\n", i);
      fail_count++;
      continue;
    }

    char line[160];
    int n = cap_audit_format_event(&event, line, sizeof(line));
    if (n < 0) {
      printf("TEST:FAIL:capability_audit_fixture:format_failed:idx=%zu\n", i);
      fail_count++;
      continue;
    }

    if (strcmp(line, kFixtureLines[i]) != 0) {
      printf("TEST:FAIL:capability_audit_fixture:line_mismatch:idx=%zu:got=%s:want=%s\n",
             i, line, kFixtureLines[i]);
      fail_count++;
      continue;
    }

    /* Mirror the audit_log test's surfacing so CI captures the byte-exact
     * lines under the fixture name as well. Helps a maintainer diff the
     * two streams if a future refactor only breaks one of them. */
    printf("AUDIT_FIXTURE:%s\n", line);
  }

  if (fail_count == 0) {
    printf("TEST:PASS:capability_audit_fixture_bytes_identical\n");
  }
}

/*
 * Non-interference: replaying the same fixture twice (with cap_reset_for_tests
 * between) must produce the same byte stream. This guards against any global
 * counter (sequence, checkpoint, seal) leaking across resets — a regression
 * that would silently corrupt audit logs across reboots.
 */
static void test_fixture_is_reset_idempotent(void) {
  cap_reset_for_tests();
  drive_fixture_sequence();

  char first_run[kFixtureLineCount][160];
  for (size_t i = 0; i < kFixtureLineCount; ++i) {
    cap_audit_event_t event = {0};
    if (cap_audit_get_for_tests(i, &event) != CAP_OK) {
      fail("first_run_missing_event");
      return;
    }
    if (cap_audit_format_event(&event, first_run[i], sizeof(first_run[i])) < 0) {
      fail("first_run_format_failed");
      return;
    }
  }

  cap_reset_for_tests();
  drive_fixture_sequence();

  for (size_t i = 0; i < kFixtureLineCount; ++i) {
    cap_audit_event_t event = {0};
    if (cap_audit_get_for_tests(i, &event) != CAP_OK) {
      fail("second_run_missing_event");
      return;
    }
    char second[160];
    if (cap_audit_format_event(&event, second, sizeof(second)) < 0) {
      fail("second_run_format_failed");
      return;
    }
    if (strcmp(first_run[i], second) != 0) {
      printf("TEST:FAIL:capability_audit_fixture:reset_drift:idx=%zu:run1=%s:run2=%s\n",
             i, first_run[i], second);
      fail_count++;
      return;
    }
  }

  printf("TEST:PASS:capability_audit_fixture_reset_idempotent\n");
}

int main(void) {
  printf("TEST:START:capability_audit_fixture\n");

  test_fixture_bytes_identical();
  test_fixture_is_reset_idempotent();

  if (fail_count != 0) {
    printf("TEST:FAIL:capability_audit_fixture:fail_count=%d\n", fail_count);
    return 1;
  }

  printf("TEST:PASS:capability_audit_fixture\n");
  return 0;
}
