/**
 * @file capability.c
 * @brief Capability type definitions and utility functions.
 *
 * Purpose:
 *   Defines the capability type enumeration-to-string mapping (cap_type_name)
 *   and provides shared constants/types used throughout the capability
 *   subsystem. Acts as the foundational type layer for capability management.
 *
 * Interactions:
 *   - cap_table.c: uses capability type definitions for grant/revoke/check.
 *   - cap_gate.c: uses capability type constants to identify required
 *     capabilities for gated operations.
 *   - event_bus.c / audit logging: uses cap_type_name() to produce
 *     human-readable audit event descriptions.
 *
 * Launched by:
 *   Not a standalone process. Provides shared definitions compiled into the
 *   kernel image and used by the capability subsystem at runtime.
 */

#include "capability.h"

#include "cap_table.h"

static cap_audit_event_t cap_audit_events[CAP_AUDIT_EVENT_MAX];
static size_t cap_audit_next_index;
static size_t cap_audit_event_count;
static size_t cap_audit_dropped_count;
static uint64_t cap_audit_next_sequence_id;

static cap_audit_checkpoint_t cap_audit_checkpoints[CAP_AUDIT_CHECKPOINT_MAX];
static size_t cap_audit_checkpoint_next_index;
static size_t cap_audit_checkpoint_count;
static uint64_t cap_audit_next_checkpoint_id;
static uint64_t cap_audit_seal;

static uint64_t cap_audit_mix_u64(uint64_t acc, uint64_t value) {
  const uint64_t prime = 1099511628211ull;
  return (acc ^ value) * prime;
}

static uint64_t cap_audit_event_digest(uint64_t sequence_id,
                                       cap_audit_op_t operation,
                                       cap_subject_id_t actor_subject_id,
                                       cap_subject_id_t subject_id,
                                       capability_id_t capability_id,
                                       cap_result_t result) {
  uint64_t digest = 1469598103934665603ull;
  digest = cap_audit_mix_u64(digest, sequence_id);
  digest = cap_audit_mix_u64(digest, (uint64_t)operation);
  digest = cap_audit_mix_u64(digest, (uint64_t)actor_subject_id);
  digest = cap_audit_mix_u64(digest, (uint64_t)subject_id);
  digest = cap_audit_mix_u64(digest, (uint64_t)capability_id);
  digest = cap_audit_mix_u64(digest, (uint64_t)result);
  return digest;
}

static void cap_audit_maybe_emit_checkpoint(uint64_t sequence_id) {
  if ((sequence_id + 1u) % CAP_AUDIT_CHECKPOINT_INTERVAL != 0u) {
    return;
  }

  cap_audit_checkpoint_t *checkpoint = &cap_audit_checkpoints[cap_audit_checkpoint_next_index];
  checkpoint->checkpoint_id = cap_audit_next_checkpoint_id;
  checkpoint->start_sequence_id = (sequence_id + 1u) - CAP_AUDIT_CHECKPOINT_INTERVAL;
  checkpoint->end_sequence_id = sequence_id;
  checkpoint->seal = cap_audit_seal;
  checkpoint->dropped_count = cap_audit_dropped_count;

  cap_audit_next_checkpoint_id++;
  cap_audit_checkpoint_next_index =
      (cap_audit_checkpoint_next_index + 1u) % CAP_AUDIT_CHECKPOINT_MAX;
  if (cap_audit_checkpoint_count < CAP_AUDIT_CHECKPOINT_MAX) {
    cap_audit_checkpoint_count++;
  }
}

static void cap_audit_record(cap_audit_op_t operation,
                             cap_subject_id_t actor_subject_id,
                             cap_subject_id_t subject_id,
                             capability_id_t capability_id,
                             cap_result_t result) {
  const uint64_t sequence_id = cap_audit_next_sequence_id;

  cap_audit_events[cap_audit_next_index].sequence_id = sequence_id;
  cap_audit_events[cap_audit_next_index].operation = operation;
  cap_audit_events[cap_audit_next_index].actor_subject_id = actor_subject_id;
  cap_audit_events[cap_audit_next_index].subject_id = subject_id;
  cap_audit_events[cap_audit_next_index].capability_id = capability_id;
  cap_audit_events[cap_audit_next_index].result = result;

  cap_audit_next_sequence_id++;
  cap_audit_next_index = (cap_audit_next_index + 1u) % CAP_AUDIT_EVENT_MAX;
  if (cap_audit_event_count < CAP_AUDIT_EVENT_MAX) {
    cap_audit_event_count++;
  } else {
    cap_audit_dropped_count++;
  }

  cap_audit_seal = cap_audit_mix_u64(cap_audit_seal,
                                     cap_audit_event_digest(sequence_id,
                                                            operation,
                                                            actor_subject_id,
                                                            subject_id,
                                                            capability_id,
                                                            result));
  cap_audit_seal = cap_audit_mix_u64(cap_audit_seal, (uint64_t)cap_audit_dropped_count);

  cap_audit_maybe_emit_checkpoint(sequence_id);
}

void cap_audit_reset_for_tests(void) {
  cap_audit_next_index = 0u;
  cap_audit_event_count = 0u;
  cap_audit_dropped_count = 0u;
  cap_audit_next_sequence_id = 0u;

  cap_audit_checkpoint_next_index = 0u;
  cap_audit_checkpoint_count = 0u;
  cap_audit_next_checkpoint_id = 0u;
  cap_audit_seal = 1469598103934665603ull;
}

size_t cap_audit_count_for_tests(void) {
  return cap_audit_event_count;
}

size_t cap_audit_dropped_for_tests(void) {
  return cap_audit_dropped_count;
}

cap_result_t cap_audit_get_for_tests(size_t index, cap_audit_event_t *out_event) {
  if (out_event == 0 || index >= cap_audit_event_count) {
    return CAP_ERR_CAP_INVALID;
  }

  size_t start = 0u;
  if (cap_audit_event_count == CAP_AUDIT_EVENT_MAX) {
    start = cap_audit_next_index;
  }

  const size_t slot = (start + index) % CAP_AUDIT_EVENT_MAX;
  *out_event = cap_audit_events[slot];
  return CAP_OK;
}

size_t cap_audit_checkpoint_count_for_tests(void) {
  return cap_audit_checkpoint_count;
}

cap_result_t cap_audit_checkpoint_get_for_tests(size_t index,
                                                cap_audit_checkpoint_t *out_checkpoint) {
  if (out_checkpoint == 0 || index >= cap_audit_checkpoint_count) {
    return CAP_ERR_CAP_INVALID;
  }

  size_t start = 0u;
  if (cap_audit_checkpoint_count == CAP_AUDIT_CHECKPOINT_MAX) {
    start = cap_audit_checkpoint_next_index;
  }

  const size_t slot = (start + index) % CAP_AUDIT_CHECKPOINT_MAX;
  *out_checkpoint = cap_audit_checkpoints[slot];
  return CAP_OK;
}

void cap_reset_for_tests(void) {
  cap_table_reset();
  cap_audit_reset_for_tests();
}

cap_result_t cap_grant_for_tests(cap_subject_id_t subject_id, capability_id_t capability_id) {
  cap_result_t result = cap_table_grant(subject_id, capability_id);
  cap_audit_record(CAP_AUDIT_OP_GRANT, subject_id, subject_id, capability_id, result);
  return result;
}

cap_result_t cap_revoke_for_tests(cap_subject_id_t subject_id, capability_id_t capability_id) {
  cap_result_t result = cap_table_revoke(subject_id, capability_id);
  cap_audit_record(CAP_AUDIT_OP_REVOKE, subject_id, subject_id, capability_id, result);
  return result;
}

static const cap_subject_id_t CAP_BOOTSTRAP_ROOT_SUBJECT_ID = 0u;

static cap_result_t cap_actor_authorized_for_mutation(cap_subject_id_t actor_subject_id) {
  return cap_table_check(actor_subject_id, CAP_CAPABILITY_ADMIN);
}

static cap_result_t cap_actor_authorized_for_admin_grant(cap_subject_id_t actor_subject_id,
                                                          capability_id_t capability_id) {
  if (capability_id == CAP_CAPABILITY_ADMIN && actor_subject_id != CAP_BOOTSTRAP_ROOT_SUBJECT_ID) {
    return CAP_ERR_MISSING;
  }

  return CAP_OK;
}

cap_result_t cap_grant_as_for_tests(cap_subject_id_t actor_subject_id,
                                    cap_subject_id_t target_subject_id,
                                    capability_id_t capability_id) {
  cap_result_t actor_check = CAP_OK;
  if (!(capability_id == CAP_CAPABILITY_ADMIN && actor_subject_id == CAP_BOOTSTRAP_ROOT_SUBJECT_ID)) {
    actor_check = cap_actor_authorized_for_mutation(actor_subject_id);
  }
  if (actor_check != CAP_OK) {
    cap_audit_record(CAP_AUDIT_OP_GRANT,
                     actor_subject_id,
                     target_subject_id,
                     capability_id,
                     actor_check);
    return actor_check;
  }

  cap_result_t admin_grant_check = cap_actor_authorized_for_admin_grant(actor_subject_id, capability_id);
  if (admin_grant_check != CAP_OK) {
    cap_audit_record(CAP_AUDIT_OP_GRANT,
                     actor_subject_id,
                     target_subject_id,
                     capability_id,
                     admin_grant_check);
    return admin_grant_check;
  }

  cap_result_t result = cap_table_grant(target_subject_id, capability_id);
  cap_audit_record(CAP_AUDIT_OP_GRANT,
                   actor_subject_id,
                   target_subject_id,
                   capability_id,
                   result);
  return result;
}

cap_result_t cap_revoke_as_for_tests(cap_subject_id_t actor_subject_id,
                                     cap_subject_id_t target_subject_id,
                                     capability_id_t capability_id) {
  cap_result_t actor_check = cap_actor_authorized_for_mutation(actor_subject_id);
  if (actor_check != CAP_OK) {
    cap_audit_record(CAP_AUDIT_OP_REVOKE,
                     actor_subject_id,
                     target_subject_id,
                     capability_id,
                     actor_check);
    return actor_check;
  }

  cap_result_t result = cap_table_revoke(target_subject_id, capability_id);
  cap_audit_record(CAP_AUDIT_OP_REVOKE,
                   actor_subject_id,
                   target_subject_id,
                   capability_id,
                   result);
  return result;
}

cap_result_t cap_check(cap_subject_id_t subject_id, capability_id_t capability_id) {
  cap_result_t result = cap_table_check(subject_id, capability_id);
  cap_audit_record(CAP_AUDIT_OP_CHECK, subject_id, subject_id, capability_id, result);
  return result;
}
