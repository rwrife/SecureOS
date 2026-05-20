/**
 * @file cap_broker.c
 * @brief Capability broker share workflow implementation.
 *
 * Purpose:
 *   Issues #85. Provides a minimal, deny-by-default broker that
 *   mediates capability sharing between an owner and a recipient.
 *   Shares are bounded to one capability + one named resource and
 *   require explicit approval. Approvals grant the underlying
 *   capability via the existing cap_table; revocation removes it.
 *
 * Interactions:
 *   - cap_table.c: cap_table_grant / cap_table_revoke side-effects
 *     occur only on explicit approve/revoke (no ambient widening).
 *   - capability.c: existing audit pipeline records grant/revoke
 *     events emitted via cap_table; broker decisions themselves do
 *     not change control flow if audit infra is busy.
 *
 * Design notes:
 *   - Fixed-size table; no dynamic allocation (kernel discipline).
 *   - Share IDs are dense 1-based indices into the table; 0 is
 *     reserved for "invalid".
 *   - Owner must currently hold the capability they are offering at
 *     request time. We do not re-check at approve time, but we keep
 *     the request fail-closed semantics (see request_share).
 */

#include "cap_broker.h"

#include "cap_table.h"
#include "capability.h"

typedef struct {
  cap_share_state_t state;
  cap_subject_id_t owner_subject_id;
  cap_subject_id_t recipient_subject_id;
  capability_id_t capability_id;
  char resource_name[CAP_BROKER_RESOURCE_NAME_MAX + 1u];
} cap_broker_share_t;

static cap_broker_share_t broker_shares[CAP_BROKER_MAX_SHARES];

static int broker_resource_copy(const char *resource_name,
                                char out[CAP_BROKER_RESOURCE_NAME_MAX + 1u]) {
  if (resource_name == ((const char *)0)) {
    return 0;
  }
  size_t i = 0u;
  while (resource_name[i] != '\0') {
    if (i >= CAP_BROKER_RESOURCE_NAME_MAX) {
      return 0;
    }
    out[i] = resource_name[i];
    i++;
  }
  if (i == 0u) {
    return 0;
  }
  out[i] = '\0';
  return 1;
}

static int broker_resource_equal(const char *a, const char *b) {
  if (a == ((const char *)0) || b == ((const char *)0)) {
    return 0;
  }
  size_t i = 0u;
  while (a[i] != '\0' && b[i] != '\0') {
    if (a[i] != b[i]) {
      return 0;
    }
    i++;
  }
  return a[i] == b[i];
}

static int broker_share_id_valid(cap_share_id_t share_id) {
  return share_id != CAP_SHARE_ID_INVALID && share_id <= CAP_BROKER_MAX_SHARES;
}

static cap_broker_share_t *broker_share_at(cap_share_id_t share_id) {
  return &broker_shares[share_id - 1u];
}

void cap_broker_reset(void) {
  for (size_t i = 0; i < CAP_BROKER_MAX_SHARES; ++i) {
    broker_shares[i].state = CAP_SHARE_STATE_NONE;
    broker_shares[i].owner_subject_id = 0u;
    broker_shares[i].recipient_subject_id = 0u;
    broker_shares[i].capability_id = (capability_id_t)0;
    broker_shares[i].resource_name[0] = '\0';
  }
}

cap_broker_result_t cap_broker_request_share(cap_subject_id_t owner_subject_id,
                                             cap_subject_id_t recipient_subject_id,
                                             capability_id_t capability_id,
                                             const char *resource_name,
                                             cap_share_id_t *out_share_id) {
  if (out_share_id == ((cap_share_id_t *)0)) {
    return CAP_BROKER_ERR_INVALID_SUBJECT;
  }
  *out_share_id = CAP_SHARE_ID_INVALID;

  if (owner_subject_id == recipient_subject_id) {
    return CAP_BROKER_ERR_INVALID_SUBJECT;
  }
  if (owner_subject_id >= CAP_TABLE_MAX_SUBJECTS ||
      recipient_subject_id >= CAP_TABLE_MAX_SUBJECTS) {
    return CAP_BROKER_ERR_INVALID_SUBJECT;
  }

  /* Owner must hold the capability they are offering. Fail closed. */
  cap_result_t owner_check = cap_table_check(owner_subject_id, capability_id);
  if (owner_check == CAP_ERR_CAP_INVALID) {
    return CAP_BROKER_ERR_INVALID_CAPABILITY;
  }
  if (owner_check != CAP_OK) {
    return CAP_BROKER_ERR_OWNER_MISSING_CAP;
  }

  char resource_copy[CAP_BROKER_RESOURCE_NAME_MAX + 1u];
  if (!broker_resource_copy(resource_name, resource_copy)) {
    return CAP_BROKER_ERR_INVALID_RESOURCE;
  }

  for (size_t i = 0; i < CAP_BROKER_MAX_SHARES; ++i) {
    if (broker_shares[i].state == CAP_SHARE_STATE_NONE) {
      cap_broker_share_t *share = &broker_shares[i];
      share->state = CAP_SHARE_STATE_REQUESTED;
      share->owner_subject_id = owner_subject_id;
      share->recipient_subject_id = recipient_subject_id;
      share->capability_id = capability_id;
      for (size_t j = 0; j <= CAP_BROKER_RESOURCE_NAME_MAX; ++j) {
        share->resource_name[j] = resource_copy[j];
        if (resource_copy[j] == '\0') {
          break;
        }
      }
      *out_share_id = (cap_share_id_t)(i + 1u);
      return CAP_BROKER_OK;
    }
  }
  return CAP_BROKER_ERR_NO_SLOT;
}

cap_broker_result_t cap_broker_approve(cap_subject_id_t approver_subject_id,
                                       cap_share_id_t share_id) {
  if (!broker_share_id_valid(share_id)) {
    return CAP_BROKER_ERR_NOT_FOUND;
  }
  cap_broker_share_t *share = broker_share_at(share_id);
  if (share->state == CAP_SHARE_STATE_NONE) {
    return CAP_BROKER_ERR_NOT_FOUND;
  }
  if (share->state != CAP_SHARE_STATE_REQUESTED) {
    return CAP_BROKER_ERR_BAD_STATE;
  }
  if (approver_subject_id != share->owner_subject_id) {
    return CAP_BROKER_ERR_NOT_AUTHORIZED;
  }

  cap_result_t grant = cap_table_grant(share->recipient_subject_id, share->capability_id);
  if (grant != CAP_OK) {
    return CAP_BROKER_ERR_INVALID_CAPABILITY;
  }
  share->state = CAP_SHARE_STATE_APPROVED;
  return CAP_BROKER_OK;
}

cap_broker_result_t cap_broker_deny(cap_subject_id_t approver_subject_id,
                                    cap_share_id_t share_id) {
  if (!broker_share_id_valid(share_id)) {
    return CAP_BROKER_ERR_NOT_FOUND;
  }
  cap_broker_share_t *share = broker_share_at(share_id);
  if (share->state == CAP_SHARE_STATE_NONE) {
    return CAP_BROKER_ERR_NOT_FOUND;
  }
  if (share->state != CAP_SHARE_STATE_REQUESTED) {
    return CAP_BROKER_ERR_BAD_STATE;
  }
  if (approver_subject_id != share->owner_subject_id) {
    return CAP_BROKER_ERR_NOT_AUTHORIZED;
  }
  share->state = CAP_SHARE_STATE_DENIED;
  return CAP_BROKER_OK;
}

cap_broker_result_t cap_broker_revoke(cap_subject_id_t actor_subject_id,
                                      cap_share_id_t share_id) {
  if (!broker_share_id_valid(share_id)) {
    return CAP_BROKER_ERR_NOT_FOUND;
  }
  cap_broker_share_t *share = broker_share_at(share_id);
  if (share->state == CAP_SHARE_STATE_NONE) {
    return CAP_BROKER_ERR_NOT_FOUND;
  }
  if (share->state != CAP_SHARE_STATE_APPROVED) {
    return CAP_BROKER_ERR_BAD_STATE;
  }
  if (actor_subject_id != share->owner_subject_id &&
      actor_subject_id != share->recipient_subject_id) {
    return CAP_BROKER_ERR_NOT_AUTHORIZED;
  }
  (void)cap_table_revoke(share->recipient_subject_id, share->capability_id);
  share->state = CAP_SHARE_STATE_REVOKED;
  return CAP_BROKER_OK;
}

cap_result_t cap_broker_recipient_check(cap_subject_id_t recipient_subject_id,
                                        capability_id_t capability_id,
                                        const char *resource_name) {
  if (recipient_subject_id >= CAP_TABLE_MAX_SUBJECTS) {
    return CAP_ERR_SUBJECT_INVALID;
  }
  if (resource_name == ((const char *)0) || resource_name[0] == '\0') {
    return CAP_ERR_MISSING;
  }
  for (size_t i = 0; i < CAP_BROKER_MAX_SHARES; ++i) {
    cap_broker_share_t *share = &broker_shares[i];
    if (share->state != CAP_SHARE_STATE_APPROVED) {
      continue;
    }
    if (share->recipient_subject_id != recipient_subject_id) {
      continue;
    }
    if (share->capability_id != capability_id) {
      continue;
    }
    if (!broker_resource_equal(share->resource_name, resource_name)) {
      continue;
    }
    /* Defense in depth: re-check underlying cap_table grant. */
    if (cap_table_check(recipient_subject_id, capability_id) != CAP_OK) {
      return CAP_ERR_MISSING;
    }
    return CAP_OK;
  }
  return CAP_ERR_MISSING;
}

cap_share_state_t cap_broker_state_for_tests(cap_share_id_t share_id) {
  if (!broker_share_id_valid(share_id)) {
    return CAP_SHARE_STATE_NONE;
  }
  return broker_share_at(share_id)->state;
}

size_t cap_broker_active_count_for_tests(void) {
  size_t count = 0u;
  for (size_t i = 0; i < CAP_BROKER_MAX_SHARES; ++i) {
    if (broker_shares[i].state == CAP_SHARE_STATE_APPROVED) {
      count++;
    }
  }
  return count;
}
