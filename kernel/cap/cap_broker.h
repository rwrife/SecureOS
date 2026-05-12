#ifndef SECUREOS_CAP_BROKER_H
#define SECUREOS_CAP_BROKER_H

/**
 * @file cap_broker.h
 * @brief Capability broker share workflow (issue #85).
 *
 * Purpose:
 *   Mediates explicit, narrow capability sharing between a holding
 *   subject (owner) and a recipient. Shares are deny-by-default,
 *   bounded to a single named resource, and revocable. The broker is
 *   the *only* sanctioned path for one subject to widen another
 *   subject's capability set on a shared resource.
 *
 * Workflow:
 *   request -> approve | deny -> (optional) revoke
 *
 * Interactions:
 *   - cap_table.c: grant/revoke side-effects on the recipient when a
 *     share is approved or revoked.
 *   - capability.c: audit events are recorded through the existing
 *     cap_audit pipeline (no new control flow).
 */

#include <stddef.h>
#include <stdint.h>

#include "capability.h"

#define CAP_BROKER_MAX_SHARES 8u
#define CAP_BROKER_RESOURCE_NAME_MAX 31u

typedef uint32_t cap_share_id_t;

#define CAP_SHARE_ID_INVALID ((cap_share_id_t)0u)

typedef enum {
  CAP_SHARE_STATE_NONE = 0,
  CAP_SHARE_STATE_REQUESTED = 1,
  CAP_SHARE_STATE_APPROVED = 2,
  CAP_SHARE_STATE_DENIED = 3,
  CAP_SHARE_STATE_REVOKED = 4,
} cap_share_state_t;

typedef enum {
  CAP_BROKER_OK = 0,
  CAP_BROKER_ERR_INVALID_SUBJECT = 1,
  CAP_BROKER_ERR_INVALID_CAPABILITY = 2,
  CAP_BROKER_ERR_INVALID_RESOURCE = 3,
  CAP_BROKER_ERR_NO_SLOT = 4,
  CAP_BROKER_ERR_NOT_FOUND = 5,
  CAP_BROKER_ERR_NOT_AUTHORIZED = 6,
  CAP_BROKER_ERR_OWNER_MISSING_CAP = 7,
  CAP_BROKER_ERR_BAD_STATE = 8,
} cap_broker_result_t;

void cap_broker_reset(void);

/**
 * Request a share. The owner must currently hold the capability they
 * are offering; otherwise the request fails closed. The recipient
 * receives nothing until the request is explicitly approved.
 */
cap_broker_result_t cap_broker_request_share(cap_subject_id_t owner_subject_id,
                                             cap_subject_id_t recipient_subject_id,
                                             capability_id_t capability_id,
                                             const char *resource_name,
                                             cap_share_id_t *out_share_id);

/**
 * Approve a previously-requested share. Approver must equal the owner
 * (explicit consent). On success the recipient is granted
 * `capability_id` in the cap_table.
 */
cap_broker_result_t cap_broker_approve(cap_subject_id_t approver_subject_id,
                                       cap_share_id_t share_id);

/**
 * Deny a pending share. No grant occurs. State becomes DENIED and the
 * slot is consumed (cannot be re-approved).
 */
cap_broker_result_t cap_broker_deny(cap_subject_id_t approver_subject_id,
                                    cap_share_id_t share_id);

/**
 * Revoke a previously-approved share. Removes the recipient's grant
 * via cap_table_revoke. Either the owner or the recipient may revoke.
 */
cap_broker_result_t cap_broker_revoke(cap_subject_id_t actor_subject_id,
                                      cap_share_id_t share_id);

/**
 * Recipient-side check: returns CAP_OK only when the recipient holds
 * an APPROVED, non-revoked share for the given capability + resource,
 * AND the underlying cap_table grant is still present.
 */
cap_result_t cap_broker_recipient_check(cap_subject_id_t recipient_subject_id,
                                        capability_id_t capability_id,
                                        const char *resource_name);

/* Read-only inspectors for tests. */
cap_share_state_t cap_broker_state_for_tests(cap_share_id_t share_id);
size_t cap_broker_active_count_for_tests(void);

#endif
