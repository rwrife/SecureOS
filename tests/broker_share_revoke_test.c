/**
 * @file broker_share_revoke_test.c
 * @brief M4 capability broker — revoke-path acceptance slice (issue #115).
 *
 * Asserts the revoke contract: owner-initiated revoke and recipient
 * self-revoke both take effect immediately, removing recipient access at
 * both the broker and the underlying cap_table layer, and the second revoke
 * lands in a stable terminal state.  Sibling of broker_share_allow_test.c /
 * broker_share_deny_test.c per plans/2026-05-14-m4-broker-acceptance-tests.md.
 *
 * Audit revoke-event assertions are gated on #98 (broker→audit wiring) and
 * emitted as TEST:SKIP until that follow-up lands.
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

static cap_share_id_t setup_approved_share(cap_subject_id_t owner,
                                           cap_subject_id_t recip,
                                           const char *name_for_failures) {
  if (cap_table_grant(owner, CAP_FS_READ) != CAP_OK) {
    marker_fail(name_for_failures, "owner_grant_setup_failed");
  }
  cap_share_id_t sid = CAP_SHARE_ID_INVALID;
  if (cap_broker_request_share(owner, recip, CAP_FS_READ, "doc-alpha", &sid) !=
      CAP_BROKER_OK) {
    marker_fail(name_for_failures, "request_share_failed");
  }
  if (cap_broker_approve(owner, sid) != CAP_BROKER_OK) {
    marker_fail(name_for_failures, "approve_failed");
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha") != CAP_OK) {
    marker_fail(name_for_failures, "post_approve_check_failed");
  }
  return sid;
}

int main(void) {
  printf("TEST:START:broker_share_revoke\n");

  cap_table_init();
  cap_table_reset();
  cap_broker_reset();

  const cap_subject_id_t owner = 1u;
  const cap_subject_id_t recip = 2u;
  const cap_subject_id_t bystander = 3u;

  /* 1: setup_grants_recipient. */
  cap_share_id_t sid =
      setup_approved_share(owner, recip, "broker_share_revoke:setup_grants_recipient");
  marker_pass("broker_share_revoke:setup_grants_recipient");

  /* 2: owner_revoke_takes_effect — recipient cannot read after revoke. */
  if (cap_broker_revoke(bystander, sid) != CAP_BROKER_ERR_NOT_AUTHORIZED) {
    marker_fail("broker_share_revoke:owner_revoke_takes_effect",
                "bystander_revoke_should_be_rejected");
  }
  if (cap_broker_revoke(owner, sid) != CAP_BROKER_OK) {
    marker_fail("broker_share_revoke:owner_revoke_takes_effect", "owner_revoke_failed");
  }
  if (cap_broker_state_for_tests(sid) != CAP_SHARE_STATE_REVOKED) {
    marker_fail("broker_share_revoke:owner_revoke_takes_effect", "state_not_revoked");
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha") != CAP_ERR_MISSING) {
    marker_fail("broker_share_revoke:owner_revoke_takes_effect",
                "recipient_should_miss_after_revoke");
  }
  marker_pass("broker_share_revoke:owner_revoke_takes_effect");

  /* 3: underlying_table_revoked — defense in depth, no stale grant. */
  if (cap_table_check(recip, CAP_FS_READ) != CAP_ERR_MISSING) {
    marker_fail("broker_share_revoke:underlying_table_revoked",
                "underlying_table_still_grants_recipient");
  }
  marker_pass("broker_share_revoke:underlying_table_revoked");

  /* 4: double_revoke_is_idempotent — terminal state, recipient still misses. */
  cap_broker_result_t second = cap_broker_revoke(owner, sid);
  if (second != CAP_BROKER_OK && second != CAP_BROKER_ERR_BAD_STATE) {
    marker_fail("broker_share_revoke:double_revoke_is_idempotent",
                "second_revoke_unexpected_result");
  }
  if (cap_broker_state_for_tests(sid) != CAP_SHARE_STATE_REVOKED) {
    marker_fail("broker_share_revoke:double_revoke_is_idempotent",
                "state_changed_after_second_revoke");
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha") != CAP_ERR_MISSING) {
    marker_fail("broker_share_revoke:double_revoke_is_idempotent",
                "recipient_regained_access_after_second_revoke");
  }
  marker_pass("broker_share_revoke:double_revoke_is_idempotent");

  /* 5: recipient_self_revoke — fresh share, recipient revokes; bystander
   *    still has no access. */
  cap_share_id_t sid2 =
      setup_approved_share(owner, recip, "broker_share_revoke:recipient_self_revoke");
  if (cap_broker_revoke(recip, sid2) != CAP_BROKER_OK) {
    marker_fail("broker_share_revoke:recipient_self_revoke",
                "recipient_self_revoke_failed");
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha") != CAP_ERR_MISSING) {
    marker_fail("broker_share_revoke:recipient_self_revoke",
                "recipient_still_has_access_after_self_revoke");
  }
  if (cap_broker_recipient_check(bystander, CAP_FS_READ, "doc-alpha") != CAP_ERR_MISSING) {
    marker_fail("broker_share_revoke:recipient_self_revoke",
                "bystander_should_not_be_granted");
  }
  marker_pass("broker_share_revoke:recipient_self_revoke");

  /* 6: audit_revoke_recorded — gated on #98. */
  marker_skip("broker_share_revoke:audit_revoke_recorded",
              "broker_audit_unwired_pending_issue_98");

  printf("TEST:DONE:broker_share_revoke\n");
  return 0;
}
