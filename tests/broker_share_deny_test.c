/**
 * @file broker_share_deny_test.c
 * @brief M4 capability broker — deny-path acceptance slice (issue #115).
 *
 * Asserts the deny-path contract: a denied share leaks nothing to the
 * recipient or any bystander, and the share is terminal (cannot be
 * re-approved). Sibling of broker_share_allow_test.c /
 * broker_share_revoke_test.c per plans/2026-05-14-m4-broker-acceptance-tests.md.
 *
 * Audit deny-event assertions are gated on #98 (broker→audit wiring) and
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

int main(void) {
  printf("TEST:START:broker_share_deny\n");

  cap_table_init();
  cap_table_reset();
  cap_broker_reset();

  const cap_subject_id_t owner = 1u;
  const cap_subject_id_t recip = 2u;
  const cap_subject_id_t bystander = 3u;

  /* 1: owner_holds_cap. */
  if (cap_table_grant(owner, CAP_FS_READ) != CAP_OK) {
    marker_fail("broker_share_deny:owner_holds_cap", "grant_setup_failed");
  }
  marker_pass("broker_share_deny:owner_holds_cap");

  /* 2: request_returns_pending_share_id. */
  cap_share_id_t sid = CAP_SHARE_ID_INVALID;
  if (cap_broker_request_share(owner, recip, CAP_FS_READ, "doc-alpha", &sid) !=
      CAP_BROKER_OK) {
    marker_fail("broker_share_deny:request_returns_pending_share_id",
                "request_share_failed");
  }
  if (sid == CAP_SHARE_ID_INVALID) {
    marker_fail("broker_share_deny:request_returns_pending_share_id",
                "share_id_invalid");
  }
  marker_pass("broker_share_deny:request_returns_pending_share_id");

  /* 3: deny_path — owner deny is a successful workflow outcome. */
  if (cap_broker_deny(owner, sid) != CAP_BROKER_OK) {
    marker_fail("broker_share_deny:deny_path", "owner_deny_failed");
  }
  if (cap_broker_state_for_tests(sid) != CAP_SHARE_STATE_DENIED) {
    marker_fail("broker_share_deny:deny_path", "state_not_denied");
  }
  marker_pass("broker_share_deny:deny_path");

  /* 4: no_recipient_grant — neither broker nor underlying table leaks. */
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha") != CAP_ERR_MISSING) {
    marker_fail("broker_share_deny:no_recipient_grant",
                "broker_check_should_miss_after_deny");
  }
  if (cap_table_check(recip, CAP_FS_READ) != CAP_ERR_MISSING) {
    marker_fail("broker_share_deny:no_recipient_grant",
                "underlying_table_should_be_clean");
  }
  marker_pass("broker_share_deny:no_recipient_grant");

  /* 5: cannot_be_re_approved — denied is terminal. */
  if (cap_broker_approve(owner, sid) != CAP_BROKER_ERR_BAD_STATE) {
    marker_fail("broker_share_deny:cannot_be_re_approved",
                "denied_share_should_not_be_reapprovable");
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha") != CAP_ERR_MISSING) {
    marker_fail("broker_share_deny:cannot_be_re_approved",
                "recipient_should_still_miss");
  }
  marker_pass("broker_share_deny:cannot_be_re_approved");

  /* 6: bystander_cannot_mutate — set up a fresh share to test approver auth. */
  cap_share_id_t sid2 = CAP_SHARE_ID_INVALID;
  if (cap_broker_request_share(owner, recip, CAP_FS_READ, "doc-alpha", &sid2) !=
      CAP_BROKER_OK) {
    marker_fail("broker_share_deny:bystander_cannot_mutate",
                "second_request_setup_failed");
  }
  if (cap_broker_approve(bystander, sid2) != CAP_BROKER_ERR_NOT_AUTHORIZED) {
    marker_fail("broker_share_deny:bystander_cannot_mutate",
                "bystander_approve_should_be_rejected");
  }
  if (cap_broker_deny(bystander, sid2) != CAP_BROKER_ERR_NOT_AUTHORIZED) {
    marker_fail("broker_share_deny:bystander_cannot_mutate",
                "bystander_deny_should_be_rejected");
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha") != CAP_ERR_MISSING) {
    marker_fail("broker_share_deny:bystander_cannot_mutate",
                "recipient_should_still_miss_after_bystander_attempts");
  }
  marker_pass("broker_share_deny:bystander_cannot_mutate");

  /* 7: audit_deny_recorded — issue #311. Set up a fresh request, then
   * call cap_broker_deny and assert a single new audit record naming
   * approver=owner, subject=recipient, cap=CAP_FS_READ, with the deny
   * encoded as CAP_ERR_MISSING (no grant taken). */
  cap_audit_reset_for_tests();
  cap_share_id_t sid_aud = CAP_SHARE_ID_INVALID;
  if (cap_broker_request_share(owner, recip, CAP_FS_READ, "doc-alpha", &sid_aud) !=
      CAP_BROKER_OK) {
    marker_fail("broker_share_deny:audit_deny_recorded",
                "audit_request_setup_failed");
  }
  size_t pre = cap_audit_count_for_tests();
  if (cap_broker_deny(owner, sid_aud) != CAP_BROKER_OK) {
    marker_fail("broker_share_deny:audit_deny_recorded", "deny_failed");
  }
  if (cap_audit_count_for_tests() != pre + 1u) {
    marker_fail("broker_share_deny:audit_deny_recorded",
                "audit_record_count_unexpected");
  }
  cap_audit_event_t ev;
  if (cap_audit_get_for_tests(pre, &ev) != CAP_OK) {
    marker_fail("broker_share_deny:audit_deny_recorded", "audit_get_failed");
  }
  if (ev.operation != CAP_AUDIT_OP_GRANT ||
      ev.actor_subject_id != owner ||
      ev.subject_id != recip ||
      ev.capability_id != CAP_FS_READ ||
      ev.result != CAP_ERR_MISSING) {
    marker_fail("broker_share_deny:audit_deny_recorded",
                "audit_record_fields_mismatch");
  }
  marker_pass("broker_share_deny:audit_deny_recorded");

  printf("TEST:DONE:broker_share_deny\n");
  return 0;
}
