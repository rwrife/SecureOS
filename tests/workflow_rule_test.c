/**
 * @file workflow_rule_test.c
 * @brief Deterministic tests for the workflow rule layer (issue #77).
 *
 * Coverage matches the validation matrix in
 * `plans/2026-04-08-zero-trust-workflow-rule-hardening.md` §"Phase 4":
 *
 *   1. allow-path: same tenant + matching scope + matching subject/cap
 *      => WORKFLOW_OK.
 *   2. cross-tenant deny: a rule registered under tenant A is invisible
 *      to tenant B; lookup returns WORKFLOW_ERR_MISSING (no existence
 *      leak) and is recorded as a deny in the workflow audit ring.
 *   3. scope-mismatch deny: read request against a write-only rule
 *      (and vice versa) returns WORKFLOW_ERR_MISSING, not a softer
 *      error.
 *   4. audit non-interference: formatting an event multiple times does
 *      not change the audit ring contents or flip the recorded
 *      outcome.
 *   5. input validation: invalid tenant / rule id / scope / subject /
 *      capability / reason all fail closed.
 *
 * Launched by:
 *   build/scripts/test_workflow_rule.sh
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/workflow_rule.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:workflow_rule:%s\n", reason);
  exit(1);
}

static void expect_eq_u64(uint64_t got, uint64_t want, const char *reason) {
  if (got != want) {
    fprintf(stderr, "expected %llu got %llu for %s\n",
            (unsigned long long)want, (unsigned long long)got, reason);
    fail(reason);
  }
}

static void expect_result(workflow_result_t got,
                          workflow_result_t want,
                          const char *reason) {
  if (got != want) {
    fprintf(stderr, "expected result %d got %d for %s\n",
            (int)want, (int)got, reason);
    fail(reason);
  }
}

int main(void) {
  printf("TEST:START:workflow_rule\n");

  /* ---------------- Phase 1: allow path ---------------- */
  workflow_rule_reset_for_tests();

  const workflow_tenant_id_t tenant_a = 7u;
  const workflow_tenant_id_t tenant_b = 9u;
  const workflow_rule_id_t   rule_read  = 100u;
  const workflow_rule_id_t   rule_write = 200u;
  const cap_subject_id_t     subject    = 3u;
  const capability_id_t      cap        = CAP_FS_READ;

  expect_result(
      workflow_rule_register(tenant_a, rule_read, WORKFLOW_SCOPE_READ,
                             subject, cap, WORKFLOW_REASON_TENANT_POLICY),
      WORKFLOW_OK, "register_read_rule");
  expect_result(
      workflow_rule_eval_read(tenant_a, rule_read, subject, cap),
      WORKFLOW_OK, "allow_path_same_tenant_matching_scope");
  printf("TEST:PASS:workflow_rule_allow_path\n");

  /* ---------------- Phase 2: cross-tenant deny ---------------- */
  expect_result(
      workflow_rule_eval_read(tenant_b, rule_read, subject, cap),
      WORKFLOW_ERR_MISSING,
      "cross_tenant_lookup_must_collapse_to_missing");

  /* Existence-leak guard: same rule id under tenant_b must remain
   * available for registration; cross-tenant probe must not have
   * created or revealed any tenant_a state to tenant_b. */
  expect_result(
      workflow_rule_register(tenant_b, rule_read, WORKFLOW_SCOPE_READ,
                             subject, cap, WORKFLOW_REASON_OPERATOR),
      WORKFLOW_OK, "tenant_b_can_register_same_rule_id");
  printf("TEST:PASS:workflow_rule_cross_tenant_deny\n");

  /* ---------------- Phase 3: scope-mismatch deny ---------------- */
  expect_result(
      workflow_rule_register(tenant_a, rule_write, WORKFLOW_SCOPE_WRITE,
                             subject, cap, WORKFLOW_REASON_AUTOMATION),
      WORKFLOW_OK, "register_write_rule");

  /* Read request against a write-only rule => missing. */
  expect_result(
      workflow_rule_eval_read(tenant_a, rule_write, subject, cap),
      WORKFLOW_ERR_MISSING,
      "read_against_write_rule_must_deny");
  /* Write request against a read-only rule => missing. */
  expect_result(
      workflow_rule_eval_write(tenant_a, rule_read, subject, cap),
      WORKFLOW_ERR_MISSING,
      "write_against_read_rule_must_deny");
  /* Mismatched subject => missing (no widening). */
  expect_result(
      workflow_rule_eval_read(tenant_a, rule_read, subject + 1u, cap),
      WORKFLOW_ERR_MISSING,
      "mismatched_subject_must_deny");
  /* Mismatched capability => missing. */
  expect_result(
      workflow_rule_eval_read(tenant_a, rule_read, subject, CAP_FS_WRITE),
      WORKFLOW_ERR_MISSING,
      "mismatched_capability_must_deny");
  printf("TEST:PASS:workflow_rule_scope_mismatch_deny\n");

  /* ---------------- Phase 4: audit non-interference ---------------- */
  /* Snapshot ring state before formatting. */
  size_t count_before   = workflow_audit_count_for_tests();
  size_t dropped_before = workflow_audit_dropped_for_tests();

  workflow_audit_event_t snap[WORKFLOW_AUDIT_EVENT_MAX];
  if (count_before > WORKFLOW_AUDIT_EVENT_MAX) {
    fail("count_before_exceeds_ring");
  }
  for (size_t i = 0; i < count_before; ++i) {
    expect_result(workflow_audit_get_for_tests(i, &snap[i]),
                  WORKFLOW_OK, "audit_snapshot_get");
  }

  /* Format every event several times. The formatter must be pure
   * and must not perturb the ring or alter the recorded outcome. */
  char buf[256];
  for (int round = 0; round < 4; ++round) {
    for (size_t i = 0; i < count_before; ++i) {
      int n = workflow_audit_format_event(&snap[i], buf, sizeof(buf));
      if (n <= 0) {
        fail("format_event_failed");
      }
      if ((size_t)n != strlen(buf)) {
        fail("format_event_length_mismatch");
      }
    }
  }

  expect_eq_u64(workflow_audit_count_for_tests(), count_before,
                "count_changed_by_formatting");
  expect_eq_u64(workflow_audit_dropped_for_tests(), dropped_before,
                "dropped_changed_by_formatting");
  for (size_t i = 0; i < count_before; ++i) {
    workflow_audit_event_t now;
    expect_result(workflow_audit_get_for_tests(i, &now),
                  WORKFLOW_OK, "audit_reread_get");
    if (memcmp(&snap[i], &now, sizeof(now)) != 0) {
      fail("audit_event_mutated_by_formatting");
    }
  }
  printf("TEST:PASS:workflow_rule_audit_non_interference\n");

  /* Allow path must remain ALLOW after every audit round. */
  expect_result(
      workflow_rule_eval_read(tenant_a, rule_read, subject, cap),
      WORKFLOW_OK, "allow_path_stable_after_audit_format");

  /* ---------------- Phase 5: input validation ---------------- */
  workflow_rule_reset_for_tests();

  expect_result(
      workflow_rule_register(WORKFLOW_RULE_TENANT_INVALID, 1u,
                             WORKFLOW_SCOPE_READ, 1u, CAP_FS_READ,
                             WORKFLOW_REASON_TENANT_POLICY),
      WORKFLOW_ERR_TENANT_INVALID, "register_rejects_invalid_tenant");
  expect_result(
      workflow_rule_register(1u, WORKFLOW_RULE_ID_INVALID,
                             WORKFLOW_SCOPE_READ, 1u, CAP_FS_READ,
                             WORKFLOW_REASON_TENANT_POLICY),
      WORKFLOW_ERR_RULE_INVALID, "register_rejects_invalid_rule_id");
  expect_result(
      workflow_rule_register(1u, 1u, WORKFLOW_SCOPE_NONE,
                             1u, CAP_FS_READ,
                             WORKFLOW_REASON_TENANT_POLICY),
      WORKFLOW_ERR_SCOPE_INVALID, "register_rejects_scope_none");
  expect_result(
      workflow_rule_register(1u, 1u, WORKFLOW_SCOPE_READ,
                             0u, CAP_FS_READ,
                             WORKFLOW_REASON_TENANT_POLICY),
      WORKFLOW_ERR_SUBJECT_INVALID, "register_rejects_zero_subject");
  expect_result(
      workflow_rule_register(1u, 1u, WORKFLOW_SCOPE_READ,
                             1u, (capability_id_t)9999,
                             WORKFLOW_REASON_TENANT_POLICY),
      WORKFLOW_ERR_CAPABILITY_INVALID,
      "register_rejects_unknown_capability");
  expect_result(
      workflow_rule_register(1u, 1u, WORKFLOW_SCOPE_READ,
                             1u, CAP_FS_READ, WORKFLOW_REASON_NONE),
      WORKFLOW_ERR_REASON_INVALID, "register_rejects_reason_none");

  /* Duplicate detection: register once, then again. */
  expect_result(
      workflow_rule_register(1u, 1u, WORKFLOW_SCOPE_READ,
                             1u, CAP_FS_READ,
                             WORKFLOW_REASON_TENANT_POLICY),
      WORKFLOW_OK, "register_initial_succeeds");
  expect_result(
      workflow_rule_register(1u, 1u, WORKFLOW_SCOPE_WRITE,
                             2u, CAP_FS_WRITE,
                             WORKFLOW_REASON_OPERATOR),
      WORKFLOW_ERR_DUPLICATE,
      "register_duplicate_must_fail_closed");

  /* Eval input validation collapses everything to MISSING (no leak). */
  expect_result(
      workflow_rule_eval_read(WORKFLOW_RULE_TENANT_INVALID, 1u, 1u, CAP_FS_READ),
      WORKFLOW_ERR_MISSING, "eval_rejects_invalid_tenant_as_missing");
  expect_result(
      workflow_rule_eval_read(1u, WORKFLOW_RULE_ID_INVALID, 1u, CAP_FS_READ),
      WORKFLOW_ERR_MISSING, "eval_rejects_invalid_rule_as_missing");
  expect_result(
      workflow_rule_eval_read(1u, 1u, 0u, CAP_FS_READ),
      WORKFLOW_ERR_MISSING, "eval_rejects_zero_subject_as_missing");
  expect_result(
      workflow_rule_eval_read(1u, 1u, 1u, (capability_id_t)9999),
      WORKFLOW_ERR_MISSING, "eval_rejects_unknown_cap_as_missing");
  printf("TEST:PASS:workflow_rule_input_validation\n");

  /* ---------------- Phase 6: capacity / ring drop ---------------- */
  workflow_rule_reset_for_tests();
  for (uint32_t i = 0; i < WORKFLOW_RULE_MAX_RULES; ++i) {
    expect_result(
        workflow_rule_register(42u, i + 1u, WORKFLOW_SCOPE_READ,
                               1u, CAP_FS_READ,
                               WORKFLOW_REASON_TENANT_POLICY),
        WORKFLOW_OK, "register_fills_slot");
  }
  expect_result(
      workflow_rule_register(42u, WORKFLOW_RULE_MAX_RULES + 1u,
                             WORKFLOW_SCOPE_READ, 1u, CAP_FS_READ,
                             WORKFLOW_REASON_TENANT_POLICY),
      WORKFLOW_ERR_NO_SLOT, "register_overflow_must_fail_closed");
  printf("TEST:PASS:workflow_rule_capacity\n");

  /* ---------------- Phase 7: audit format spot-check ---------------- */
  workflow_rule_reset_for_tests();
  expect_result(
      workflow_rule_register(7u, 100u, WORKFLOW_SCOPE_READ,
                             3u, CAP_FS_READ,
                             WORKFLOW_REASON_TENANT_POLICY),
      WORKFLOW_OK, "audit_fmt_setup_register");
  expect_result(
      workflow_rule_eval_read(7u, 100u, 3u, CAP_FS_READ),
      WORKFLOW_OK, "audit_fmt_setup_eval");

  workflow_audit_event_t e;
  expect_result(workflow_audit_get_for_tests(1u, &e),
                WORKFLOW_OK, "audit_fmt_get_eval_event");
  char line[256];
  int n = workflow_audit_format_event(&e, line, sizeof(line));
  if (n <= 0) {
    fail("audit_fmt_returned_negative");
  }
  const char *expected =
      "WORKFLOW_AUDIT:seq=1:op=EVAL_READ:tenant=7:rule=100:scope=READ"
      ":subject=3:cap=6:reason=TENANT_POLICY:result=OK:outcome=ALLOW";
  if (strcmp(line, expected) != 0) {
    fprintf(stderr, "got:  %s\nwant: %s\n", line, expected);
    fail("audit_fmt_line_mismatch");
  }
  printf("TEST:PASS:workflow_rule_audit_format\n");

  printf("TEST:DONE:workflow_rule\n");
  return 0;
}
