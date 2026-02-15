#include "capability.h"

#include "cap_table.h"

void cap_reset_for_tests(void) {
  cap_table_reset();
}

cap_result_t cap_grant_for_tests(cap_subject_id_t subject_id, capability_id_t capability_id) {
  return cap_table_grant(subject_id, capability_id);
}

cap_result_t cap_revoke_for_tests(cap_subject_id_t subject_id, capability_id_t capability_id) {
  return cap_table_revoke(subject_id, capability_id);
}

cap_result_t cap_check(cap_subject_id_t subject_id, capability_id_t capability_id) {
  return cap_table_check(subject_id, capability_id);
}
