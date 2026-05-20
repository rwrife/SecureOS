/**
 * @file broker_share_allow_test.c
 * @brief M4 capability broker — allow-path acceptance slice (issue #115).
 *
 * One of three deterministic acceptance validators carved out of
 * plans/2026-05-14-m4-broker-acceptance-tests.md for BUILD_ROADMAP §5.4.
 * Sibling files: broker_share_deny_test.c, broker_share_revoke_test.c.
 *
 * Purpose:
 *   Asserts the broker's allow-path contract from the recipient's point of
 *   view (the cap_broker_test.c unit covers internal state transitions; this
 *   slice covers the contract the launcher / consumer side will rely on).
 *   Emits structured TEST:PASS:<name> / TEST:FAIL:<name>:<reason> markers
 *   consumable by the validator JSON report (#110).
 *
 * Audit assertions (per plan §"audit_grant_recorded") are gated behind #98
 * (capability audit serial line) and emit TEST:SKIP markers here because the
 * broker does not yet route through the cap_audit pipeline. This keeps the
 * slice mergeable while #98's broker wiring is filed as a follow-up.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_broker.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"

static void marker_fail(const char *name, const char *reason) {
  printf("TEST:FAIL:%s:%s\n", name, reason);
  exit(1);
}

static void marker_pass(const char *name) { printf("TEST:PASS:%s\n", name); }

static void marker_skip(const char *name, const char *reason) {
  printf("TEST:SKIP:%s:%s\n", name, reason);
}

int main(void) {
  printf("TEST:START:broker_share_allow\n");

  cap_table_init();
  cap_table_reset();
  cap_broker_reset();

  const cap_subject_id_t owner = 1u;
  const cap_subject_id_t recip = 2u;

  /* 1: owner_holds_cap — broker precondition. */
  if (cap_table_grant(owner, CAP_FS_READ) != CAP_OK) {
    marker_fail("broker_share_allow:owner_holds_cap", "grant_setup_failed");
  }
  if (cap_table_check(owner, CAP_FS_READ) != CAP_OK) {
    marker_fail("broker_share_allow:owner_holds_cap", "check_after_grant_missing");
  }
  marker_pass("broker_share_allow:owner_holds_cap");

  /* 2: request_returns_pending_share_id — pre-approval recipient has nothing. */
  cap_share_id_t sid = CAP_SHARE_ID_INVALID;
  if (cap_broker_request_share(owner, recip, CAP_FS_READ, "doc-alpha", &sid) !=
      CAP_BROKER_OK) {
    marker_fail("broker_share_allow:request_returns_pending_share_id",
                "request_share_failed");
  }
  if (sid == CAP_SHARE_ID_INVALID) {
    marker_fail("broker_share_allow:request_returns_pending_share_id",
                "share_id_invalid");
  }
  if (cap_broker_state_for_tests(sid) != CAP_SHARE_STATE_REQUESTED) {
    marker_fail("broker_share_allow:request_returns_pending_share_id",
                "state_not_requested");
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha") != CAP_ERR_MISSING) {
    marker_fail("broker_share_allow:request_returns_pending_share_id",
                "recipient_check_should_fail_before_approve");
  }
  marker_pass("broker_share_allow:request_returns_pending_share_id");

  /* 3: approve_grants_recipient. */
  if (cap_broker_approve(owner, sid) != CAP_BROKER_OK) {
    marker_fail("broker_share_allow:approve_grants_recipient", "approve_failed");
  }
  if (cap_broker_state_for_tests(sid) != CAP_SHARE_STATE_APPROVED) {
    marker_fail("broker_share_allow:approve_grants_recipient", "state_not_approved");
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha") != CAP_OK) {
    marker_fail("broker_share_allow:approve_grants_recipient",
                "recipient_check_after_approve_failed");
  }
  marker_pass("broker_share_allow:approve_grants_recipient");

  /* 4: scope_is_resource_bound — different resource name must miss. */
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-beta") != CAP_ERR_MISSING) {
    marker_fail("broker_share_allow:scope_is_resource_bound",
                "other_resource_should_not_be_granted");
  }
  marker_pass("broker_share_allow:scope_is_resource_bound");

  /* 5: scope_is_capability_bound — different cap must miss. */
  if (cap_broker_recipient_check(recip, CAP_FS_WRITE, "doc-alpha") != CAP_ERR_MISSING) {
    marker_fail("broker_share_allow:scope_is_capability_bound",
                "other_capability_should_not_be_granted");
  }
  marker_pass("broker_share_allow:scope_is_capability_bound");

  /* 6: audit_grant_recorded — gated on #98 broker→audit wiring. */
  marker_skip("broker_share_allow:audit_grant_recorded",
              "broker_audit_unwired_pending_issue_98");

  printf("TEST:DONE:broker_share_allow\n");
  return 0;
}
