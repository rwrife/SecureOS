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

void cap_audit_emit(cap_audit_op_t operation,
                    cap_subject_id_t actor_subject_id,
                    cap_subject_id_t subject_id,
                    capability_id_t capability_id,
                    cap_result_t result) {
  /* Issue #311: public entry point for broker_svc / fs_svc to publish into
   * the same audit ring used by cap_check / cap_grant_as_for_tests. */
  cap_audit_record(operation, actor_subject_id, subject_id, capability_id, result);
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

/*
 * Audit event serialization.
 *
 * These helpers do not touch any module-level audit state. They translate the
 * already-recorded event tuple into a stable textual form for serial logs and
 * tests. Non-interference: callers can format an event any number of times
 * without affecting capability decisions or the audit ring.
 */

cap_audit_outcome_t cap_audit_event_outcome(const cap_audit_event_t *event) {
  if (event == 0) {
    return CAP_AUDIT_OUTCOME_DENY;
  }

  switch (event->operation) {
    case CAP_AUDIT_OP_CHECK:
      return (event->result == CAP_OK) ? CAP_AUDIT_OUTCOME_ALLOW
                                       : CAP_AUDIT_OUTCOME_DENY;
    case CAP_AUDIT_OP_GRANT:
      return (event->result == CAP_OK) ? CAP_AUDIT_OUTCOME_GRANTED
                                       : CAP_AUDIT_OUTCOME_GRANT_DENIED;
    case CAP_AUDIT_OP_REVOKE:
      return (event->result == CAP_OK) ? CAP_AUDIT_OUTCOME_REVOKED
                                       : CAP_AUDIT_OUTCOME_REVOKE_DENIED;
    case CAP_AUDIT_OP_CASCADE_REVOKE:
      return CAP_AUDIT_OUTCOME_CASCADE_REVOKED;
    case CAP_AUDIT_OP_CASCADE_DONE:
      return CAP_AUDIT_OUTCOME_CASCADE_DONE;
  }

  return CAP_AUDIT_OUTCOME_DENY;
}

static const char *cap_audit_op_name(cap_audit_op_t op) {
  switch (op) {
    case CAP_AUDIT_OP_CHECK:  return "CHECK";
    case CAP_AUDIT_OP_GRANT:  return "GRANT";
    case CAP_AUDIT_OP_REVOKE: return "REVOKE";
    case CAP_AUDIT_OP_CASCADE_REVOKE: return "CASCADE_REVOKE";
    case CAP_AUDIT_OP_CASCADE_DONE:   return "CASCADE_DONE";
  }
  return "UNKNOWN";
}

static const char *cap_audit_result_name(cap_result_t result) {
  switch (result) {
    case CAP_OK:                  return "OK";
    case CAP_ERR_MISSING:         return "MISSING";
    case CAP_ERR_SUBJECT_INVALID: return "SUBJECT_INVALID";
    case CAP_ERR_CAP_INVALID:     return "CAP_INVALID";
  }
  return "UNKNOWN";
}

static const char *cap_audit_outcome_name(cap_audit_outcome_t outcome) {
  switch (outcome) {
    case CAP_AUDIT_OUTCOME_ALLOW:          return "ALLOW";
    case CAP_AUDIT_OUTCOME_DENY:           return "DENY";
    case CAP_AUDIT_OUTCOME_GRANTED:        return "GRANTED";
    case CAP_AUDIT_OUTCOME_GRANT_DENIED:   return "GRANT_DENIED";
    case CAP_AUDIT_OUTCOME_REVOKED:        return "REVOKED";
    case CAP_AUDIT_OUTCOME_REVOKE_DENIED:  return "REVOKE_DENIED";
    case CAP_AUDIT_OUTCOME_CASCADE_REVOKED: return "CASCADE_REVOKED";
    case CAP_AUDIT_OUTCOME_CASCADE_DONE:    return "CASCADE_DONE";
  }
  return "UNKNOWN";
}

/* Minimal freestanding decimal writer. Writes `value` into `buf` and returns
 * the number of bytes written, or -1 if `buf_size` is too small. */
static int cap_audit_write_u64(uint64_t value, char *buf, size_t buf_size) {
  char tmp[21];
  size_t n = 0u;
  if (value == 0u) {
    tmp[n++] = '0';
  } else {
    while (value > 0u && n < sizeof(tmp)) {
      tmp[n++] = (char)('0' + (int)(value % 10u));
      value /= 10u;
    }
  }
  if (n > buf_size) {
    return -1;
  }
  for (size_t i = 0; i < n; ++i) {
    buf[i] = tmp[n - 1u - i];
  }
  return (int)n;
}

static int cap_audit_write_str(const char *s, char *buf, size_t buf_size) {
  size_t n = 0u;
  while (s[n] != '\0') {
    if (n >= buf_size) {
      return -1;
    }
    buf[n] = s[n];
    ++n;
  }
  return (int)n;
}

#define CAP_AUDIT_APPEND_STR(literal)                                          \
  do {                                                                         \
    int w = cap_audit_write_str((literal), buf + pos, buf_size - 1u - pos);   \
    if (w < 0) return -1;                                                      \
    pos += (size_t)w;                                                          \
  } while (0)

#define CAP_AUDIT_APPEND_U64(value)                                            \
  do {                                                                         \
    int w = cap_audit_write_u64((value), buf + pos, buf_size - 1u - pos);     \
    if (w < 0) return -1;                                                      \
    pos += (size_t)w;                                                          \
  } while (0)

int cap_audit_format_event(const cap_audit_event_t *event,
                           char *buf,
                           size_t buf_size) {
  if (event == 0 || buf == 0 || buf_size == 0u) {
    return -1;
  }

  size_t pos = 0u;
  CAP_AUDIT_APPEND_STR("CAP_AUDIT:seq=");
  CAP_AUDIT_APPEND_U64(event->sequence_id);
  CAP_AUDIT_APPEND_STR(":op=");
  CAP_AUDIT_APPEND_STR(cap_audit_op_name(event->operation));
  CAP_AUDIT_APPEND_STR(":actor=");
  CAP_AUDIT_APPEND_U64((uint64_t)event->actor_subject_id);
  CAP_AUDIT_APPEND_STR(":subject=");
  CAP_AUDIT_APPEND_U64((uint64_t)event->subject_id);
  CAP_AUDIT_APPEND_STR(":cap=");
  CAP_AUDIT_APPEND_U64((uint64_t)event->capability_id);
  CAP_AUDIT_APPEND_STR(":result=");
  CAP_AUDIT_APPEND_STR(cap_audit_result_name(event->result));
  CAP_AUDIT_APPEND_STR(":outcome=");
  CAP_AUDIT_APPEND_STR(cap_audit_outcome_name(cap_audit_event_outcome(event)));

  buf[pos] = '\0';
  return (int)pos;
}
