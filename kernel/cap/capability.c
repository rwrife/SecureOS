#include "capability.h"

#include "cap_table.h"

static cap_audit_event_t cap_audit_events[CAP_AUDIT_EVENT_MAX];
static size_t cap_audit_next_index;
static size_t cap_audit_event_count;
static size_t cap_audit_dropped_count;

static void cap_audit_record(cap_audit_op_t operation,
                             cap_subject_id_t actor_subject_id,
                             cap_subject_id_t subject_id,
                             capability_id_t capability_id,
                             cap_result_t result) {
  cap_audit_events[cap_audit_next_index].operation = operation;
  cap_audit_events[cap_audit_next_index].actor_subject_id = actor_subject_id;
  cap_audit_events[cap_audit_next_index].subject_id = subject_id;
  cap_audit_events[cap_audit_next_index].capability_id = capability_id;
  cap_audit_events[cap_audit_next_index].result = result;

  cap_audit_next_index = (cap_audit_next_index + 1u) % CAP_AUDIT_EVENT_MAX;
  if (cap_audit_event_count < CAP_AUDIT_EVENT_MAX) {
    cap_audit_event_count++;
  } else {
    cap_audit_dropped_count++;
  }
}

void cap_audit_reset_for_tests(void) {
  cap_audit_next_index = 0u;
  cap_audit_event_count = 0u;
  cap_audit_dropped_count = 0u;
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
