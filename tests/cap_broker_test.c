/**
 * @file cap_broker_test.c
 * @brief Tests for the capability broker share workflow (issue #85).
 *
 * Purpose:
 *   Validates allow / deny / revoke behavior of the capability broker:
 *   - Owner cannot share a capability they do not hold (fail closed).
 *   - Approval grants the recipient access through cap_broker_recipient_check.
 *   - Deny leaves the recipient with no access.
 *   - Revoke removes recipient access and is idempotent against re-revoke.
 *   - Non-participants cannot approve or revoke.
 *   - Resource scoping: an approved share for "alpha" does not satisfy
 *     a check for "beta".
 *
 * Launched by:
 *   build/scripts/test_cap_broker.sh.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_broker.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:cap_broker:%s\n", reason);
  exit(1);
}

int main(void) {
  printf("TEST:START:cap_broker\n");

  cap_table_init();
  cap_broker_reset();

  const cap_subject_id_t owner = 1u;
  const cap_subject_id_t recipient = 2u;
  const cap_subject_id_t bystander = 3u;

  /* Phase 1: owner without the capability cannot offer a share. */
  cap_share_id_t sid = CAP_SHARE_ID_INVALID;
  if (cap_broker_request_share(owner, recipient, CAP_FS_READ, "doc-alpha", &sid) !=
      CAP_BROKER_ERR_OWNER_MISSING_CAP) {
    fail("owner_missing_cap_should_fail_closed");
  }
  if (sid != CAP_SHARE_ID_INVALID) {
    fail("owner_missing_cap_should_not_allocate");
  }
  printf("TEST:PASS:cap_broker_owner_must_hold_cap\n");

  /* Bootstrap: grant the owner the capability they will share. */
  if (cap_table_grant(owner, CAP_FS_READ) != CAP_OK) {
    fail("setup_owner_grant_failed");
  }

  /* Phase 2: invalid recipient (self-share) and resource name. */
  if (cap_broker_request_share(owner, owner, CAP_FS_READ, "doc-alpha", &sid) !=
      CAP_BROKER_ERR_INVALID_SUBJECT) {
    fail("self_share_should_be_rejected");
  }
  if (cap_broker_request_share(owner, recipient, CAP_FS_READ, NULL, &sid) !=
      CAP_BROKER_ERR_INVALID_RESOURCE) {
    fail("null_resource_should_be_rejected");
  }
  if (cap_broker_request_share(owner, recipient, CAP_FS_READ, "", &sid) !=
      CAP_BROKER_ERR_INVALID_RESOURCE) {
    fail("empty_resource_should_be_rejected");
  }
  printf("TEST:PASS:cap_broker_request_input_validation\n");

  /* Phase 3: deny path. Recipient gets nothing on a denied request. */
  cap_share_id_t deny_sid = CAP_SHARE_ID_INVALID;
  if (cap_broker_request_share(owner, recipient, CAP_FS_READ, "doc-alpha", &deny_sid) !=
      CAP_BROKER_OK) {
    fail("request_deny_setup_failed");
  }
  if (cap_broker_deny(bystander, deny_sid) != CAP_BROKER_ERR_NOT_AUTHORIZED) {
    fail("bystander_should_not_deny");
  }
  if (cap_broker_deny(owner, deny_sid) != CAP_BROKER_OK) {
    fail("owner_deny_failed");
  }
  if (cap_broker_state_for_tests(deny_sid) != CAP_SHARE_STATE_DENIED) {
    fail("deny_state_not_recorded");
  }
  if (cap_broker_recipient_check(recipient, CAP_FS_READ, "doc-alpha") != CAP_ERR_MISSING) {
    fail("denied_share_should_not_grant_access");
  }
  /* A denied request cannot be approved later. */
  if (cap_broker_approve(owner, deny_sid) != CAP_BROKER_ERR_BAD_STATE) {
    fail("denied_should_not_be_reapprovable");
  }
  printf("TEST:PASS:cap_broker_deny_path\n");

  /* Phase 4: allow path. Approval grants the recipient. */
  cap_share_id_t allow_sid = CAP_SHARE_ID_INVALID;
  if (cap_broker_request_share(owner, recipient, CAP_FS_READ, "doc-alpha", &allow_sid) !=
      CAP_BROKER_OK) {
    fail("request_allow_setup_failed");
  }
  if (cap_broker_approve(bystander, allow_sid) != CAP_BROKER_ERR_NOT_AUTHORIZED) {
    fail("bystander_should_not_approve");
  }
  if (cap_broker_recipient_check(recipient, CAP_FS_READ, "doc-alpha") != CAP_ERR_MISSING) {
    fail("pre_approval_recipient_should_not_have_access");
  }
  if (cap_broker_approve(owner, allow_sid) != CAP_BROKER_OK) {
    fail("owner_approve_failed");
  }
  if (cap_broker_state_for_tests(allow_sid) != CAP_SHARE_STATE_APPROVED) {
    fail("approve_state_not_recorded");
  }
  if (cap_broker_recipient_check(recipient, CAP_FS_READ, "doc-alpha") != CAP_OK) {
    fail("approved_share_should_grant_access");
  }
  if (cap_broker_active_count_for_tests() != 1u) {
    fail("active_share_count_mismatch");
  }
  /* Resource scope: a different resource name must not satisfy the check. */
  if (cap_broker_recipient_check(recipient, CAP_FS_READ, "doc-beta") != CAP_ERR_MISSING) {
    fail("approved_share_should_be_resource_scoped");
  }
  /* Capability scope: the approved share must not bleed into other caps. */
  if (cap_broker_recipient_check(recipient, CAP_FS_WRITE, "doc-alpha") != CAP_ERR_MISSING) {
    fail("approved_share_should_be_capability_scoped");
  }
  /* Re-approving an already-approved share is a state error. */
  if (cap_broker_approve(owner, allow_sid) != CAP_BROKER_ERR_BAD_STATE) {
    fail("double_approve_should_be_bad_state");
  }
  printf("TEST:PASS:cap_broker_allow_path\n");

  /* Phase 5: revoke. */
  if (cap_broker_revoke(bystander, allow_sid) != CAP_BROKER_ERR_NOT_AUTHORIZED) {
    fail("bystander_should_not_revoke");
  }
  if (cap_broker_revoke(owner, allow_sid) != CAP_BROKER_OK) {
    fail("owner_revoke_failed");
  }
  if (cap_broker_state_for_tests(allow_sid) != CAP_SHARE_STATE_REVOKED) {
    fail("revoke_state_not_recorded");
  }
  if (cap_broker_recipient_check(recipient, CAP_FS_READ, "doc-alpha") != CAP_ERR_MISSING) {
    fail("revoked_share_should_remove_access");
  }
  if (cap_broker_active_count_for_tests() != 0u) {
    fail("revoked_should_not_count_as_active");
  }
  /* Double revoke is a state error, not a silent success. */
  if (cap_broker_revoke(owner, allow_sid) != CAP_BROKER_ERR_BAD_STATE) {
    fail("double_revoke_should_be_bad_state");
  }
  printf("TEST:PASS:cap_broker_revoke_path\n");

  /* Phase 6: recipient may also revoke their own share. */
  if (cap_table_grant(owner, CAP_FS_READ) != CAP_OK) {
    fail("setup_recip_revoke_grant_failed");
  }
  cap_share_id_t recip_sid = CAP_SHARE_ID_INVALID;
  if (cap_broker_request_share(owner, recipient, CAP_FS_READ, "doc-alpha", &recip_sid) !=
      CAP_BROKER_OK) {
    fail("recip_revoke_request_failed");
  }
  if (cap_broker_approve(owner, recip_sid) != CAP_BROKER_OK) {
    fail("recip_revoke_approve_failed");
  }
  if (cap_broker_recipient_check(recipient, CAP_FS_READ, "doc-alpha") != CAP_OK) {
    fail("recip_revoke_pre_check_failed");
  }
  if (cap_broker_revoke(recipient, recip_sid) != CAP_BROKER_OK) {
    fail("recipient_self_revoke_failed");
  }
  if (cap_broker_recipient_check(recipient, CAP_FS_READ, "doc-alpha") != CAP_ERR_MISSING) {
    fail("recipient_self_revoke_did_not_remove_access");
  }
  printf("TEST:PASS:cap_broker_recipient_self_revoke\n");

  /* Phase 7: non-existent share IDs are reported, not silently ignored. */
  if (cap_broker_approve(owner, CAP_SHARE_ID_INVALID) != CAP_BROKER_ERR_NOT_FOUND) {
    fail("invalid_share_id_should_be_not_found");
  }
  if (cap_broker_approve(owner, (cap_share_id_t)(CAP_BROKER_MAX_SHARES + 1u)) !=
      CAP_BROKER_ERR_NOT_FOUND) {
    fail("out_of_range_share_id_should_be_not_found");
  }
  printf("TEST:PASS:cap_broker_invalid_ids\n");

  /* Phase 8: reset clears all state. */
  cap_broker_reset();
  if (cap_broker_active_count_for_tests() != 0u) {
    fail("reset_should_clear_active_shares");
  }
  printf("TEST:PASS:cap_broker_reset_clears_state\n");

  printf("TEST:DONE:cap_broker\n");
  return 0;
}
