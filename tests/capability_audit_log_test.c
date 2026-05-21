/**
 * @file capability_audit_log_test.c
 * @brief Deterministic tests for the capability audit log slice.
 *
 * Purpose:
 *   Exercises the audit-log serialization slice described in
 *   plans/2026-04-18-capability-audit-deny-log.md and tracked by issue #84:
 *     - Phase 1: stable record shape + single serialization format.
 *     - Phase 2: capture for grant + deny paths.
 *     - Phase 4: non-interference (formatting/logging does not alter policy).
 *
 *   These tests do not depend on any device or serial sink. They operate on
 *   the audit events captured by cap_audit_get_for_tests() and on the pure
 *   cap_audit_format_event() helper, which is also what the kernel-side
 *   serial log path will use to emit audit lines.
 *
 * Interactions:
 *   - kernel/cap/capability.{h,c}: audit ring + formatter under test.
 *   - kernel/cap/cap_table.c: capability decisions used to populate the ring.
 *
 * Launched by:
 *   Compiled and executed by build/scripts/test_capability_audit_log.sh,
 *   wired into build/scripts/test.sh as the `capability_audit_log` target.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/capability.h"

static int fail_count;

static void fail(const char *reason) {
  printf("TEST:FAIL:capability_audit_log:%s\n", reason);
  fail_count++;
}

static cap_audit_event_t get_event(size_t index, const char *reason) {
  cap_audit_event_t event = {0};
  if (cap_audit_get_for_tests(index, &event) != CAP_OK) {
    fail(reason);
  }
  return event;
}

/*
 * Phase 1 + Phase 2: grant + deny capture, structured + stable.
 *
 * Walks a representative sequence (deny check, grant, allow check, revoke,
 * denied recheck) and asserts each emitted line exactly matches the contract
 * documented in capability.h.
 */
static void test_format_grant_and_deny_lines(void) {
  cap_reset_for_tests();

  /* Phase 2 capture: deny path at the syscall/gate boundary. */
  if (cap_check(1u, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("deny_check_result");
  }
  /* Phase 2 capture: launcher-style grant. */
  if (cap_grant_for_tests(1u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("grant_failed");
  }
  /* Allow path after grant. */
  if (cap_check(1u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("allow_check_result");
  }
  /* Revoke + post-revoke deny. */
  if (cap_revoke_for_tests(1u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("revoke_failed");
  }
  if (cap_check(1u, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("post_revoke_deny_result");
  }

  const char *expected[] = {
    "CAP_AUDIT:seq=0:op=CHECK:actor=1:subject=1:cap=1:result=MISSING:outcome=DENY",
    "CAP_AUDIT:seq=1:op=GRANT:actor=1:subject=1:cap=1:result=OK:outcome=GRANTED",
    "CAP_AUDIT:seq=2:op=CHECK:actor=1:subject=1:cap=1:result=OK:outcome=ALLOW",
    "CAP_AUDIT:seq=3:op=REVOKE:actor=1:subject=1:cap=1:result=OK:outcome=REVOKED",
    "CAP_AUDIT:seq=4:op=CHECK:actor=1:subject=1:cap=1:result=MISSING:outcome=DENY",
  };

  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
    cap_audit_event_t event = get_event(i, "missing_event");
    char line[160];
    int n = cap_audit_format_event(&event, line, sizeof(line));
    if (n < 0) {
      fail("format_failed");
      continue;
    }
    if (strcmp(line, expected[i]) != 0) {
      printf("TEST:FAIL:capability_audit_log:line_mismatch:idx=%zu:got=%s:want=%s\n",
             i, line, expected[i]);
      fail_count++;
      continue;
    }
    /* Surface the formatted record so CI captures the deny-log shape. */
    printf("AUDIT_LOG:%s\n", line);
  }

  printf("TEST:PASS:capability_audit_log_grant_and_deny\n");
}

/*
 * Phase 1: invalid-input handling on the formatter (deny-by-default).
 */
static void test_format_invalid_inputs(void) {
  char buf[64];
  cap_audit_event_t event = {0};

  if (cap_audit_format_event(0, buf, sizeof(buf)) != -1) {
    fail("format_null_event_should_fail");
  }
  if (cap_audit_format_event(&event, 0, sizeof(buf)) != -1) {
    fail("format_null_buf_should_fail");
  }
  if (cap_audit_format_event(&event, buf, 0u) != -1) {
    fail("format_zero_size_should_fail");
  }
  /* Tiny buffer should not write past the end. */
  char tiny[8];
  memset(tiny, 0x55, sizeof(tiny));
  if (cap_audit_format_event(&event, tiny, sizeof(tiny)) != -1) {
    fail("format_tiny_buf_should_fail");
  }

  printf("TEST:PASS:capability_audit_log_invalid_inputs\n");
}

/*
 * Phase 4: non-interference. Formatting any number of audit events must not
 * change capability decisions, the audit ring, the dropped count, the ring
 * sequence cursor, or the checkpoint state. This is the contract that lets
 * the serial-first audit sink be added without altering policy.
 */
static void test_format_does_not_alter_policy(void) {
  cap_reset_for_tests();

  /* Build a small mixed history. */
  (void)cap_check(2u, CAP_SERIAL_WRITE);            /* deny */
  (void)cap_grant_for_tests(2u, CAP_SERIAL_WRITE);
  (void)cap_check(2u, CAP_SERIAL_WRITE);            /* allow */
  (void)cap_revoke_for_tests(2u, CAP_SERIAL_WRITE);
  (void)cap_check(2u, CAP_SERIAL_WRITE);            /* deny again */

  size_t before_count = cap_audit_count_for_tests();
  size_t before_dropped = cap_audit_dropped_for_tests();
  size_t before_checkpoints = cap_audit_checkpoint_count_for_tests();

  cap_audit_event_t before_events[CAP_AUDIT_EVENT_MAX];
  for (size_t i = 0; i < before_count; ++i) {
    before_events[i] = get_event(i, "snapshot_failed");
  }

  /* Format every event many times. If any of this leaked into the ring or
   * the table, the post-format state would diverge. */
  for (int round = 0; round < 4; ++round) {
    for (size_t i = 0; i < before_count; ++i) {
      char line[160];
      if (cap_audit_format_event(&before_events[i], line, sizeof(line)) < 0) {
        fail("format_round_failed");
      }
    }
  }

  if (cap_audit_count_for_tests() != before_count) {
    fail("ring_count_changed_by_format");
  }
  if (cap_audit_dropped_for_tests() != before_dropped) {
    fail("dropped_count_changed_by_format");
  }
  if (cap_audit_checkpoint_count_for_tests() != before_checkpoints) {
    fail("checkpoint_count_changed_by_format");
  }
  for (size_t i = 0; i < before_count; ++i) {
    cap_audit_event_t after = get_event(i, "post_format_snapshot_failed");
    if (after.sequence_id      != before_events[i].sequence_id      ||
        after.operation        != before_events[i].operation        ||
        after.actor_subject_id != before_events[i].actor_subject_id ||
        after.subject_id       != before_events[i].subject_id       ||
        after.capability_id    != before_events[i].capability_id    ||
        after.result           != before_events[i].result) {
      fail("event_mutated_by_format");
    }
  }

  /* Capability decisions must still match the same history. */
  if (cap_check(2u, CAP_SERIAL_WRITE) != CAP_ERR_MISSING) {
    fail("post_format_deny_changed");
  }
  if (cap_grant_for_tests(2u, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("post_format_grant_changed");
  }
  if (cap_check(2u, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("post_format_allow_changed");
  }

  printf("TEST:PASS:capability_audit_log_non_interference\n");
}

int main(void) {
  printf("TEST:START:capability_audit_log\n");

  test_format_grant_and_deny_lines();
  test_format_invalid_inputs();
  test_format_does_not_alter_policy();

  if (fail_count != 0) {
    printf("TEST:FAIL:capability_audit_log:fail_count=%d\n", fail_count);
    return 1;
  }

  printf("TEST:PASS:capability_audit_log\n");
  return 0;
}
