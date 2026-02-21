#include "capability.h"

#include "cap_table.h"

static cap_audit_event_t cap_audit_events[CAP_AUDIT_EVENT_MAX];
static size_t cap_audit_next_index;
static size_t cap_audit_event_count;

static void cap_audit_record(cap_audit_op_t operation,
                             cap_subject_id_t subject_id,
                             capability_id_t capability_id,
                             cap_result_t result) {
  cap_audit_events[cap_audit_next_index].operation = operation;
  cap_audit_events[cap_audit_next_index].subject_id = subject_id;
  cap_audit_events[cap_audit_next_index].capability_id = capability_id;
  cap_audit_events[cap_audit_next_index].result = result;

  cap_audit_next_index = (cap_audit_next_index + 1u) % CAP_AUDIT_EVENT_MAX;
  if (cap_audit_event_count < CAP_AUDIT_EVENT_MAX) {
    cap_audit_event_count++;
  }
}

void cap_audit_reset_for_tests(void) {
  cap_audit_next_index = 0u;
  cap_audit_event_count = 0u;
}

size_t cap_audit_count_for_tests(void) {
  return cap_audit_event_count;
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
  cap_audit_record(CAP_AUDIT_OP_GRANT, subject_id, capability_id, result);
  return result;
}

cap_result_t cap_revoke_for_tests(cap_subject_id_t subject_id, capability_id_t capability_id) {
  cap_result_t result = cap_table_revoke(subject_id, capability_id);
  cap_audit_record(CAP_AUDIT_OP_REVOKE, subject_id, capability_id, result);
  return result;
}

cap_result_t cap_check(cap_subject_id_t subject_id, capability_id_t capability_id) {
  cap_result_t result = cap_table_check(subject_id, capability_id);
  cap_audit_record(CAP_AUDIT_OP_CHECK, subject_id, capability_id, result);
  return result;
}
