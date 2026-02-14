#ifndef SECUREOS_CAP_TABLE_H
#define SECUREOS_CAP_TABLE_H

#include <stddef.h>

#include "capability.h"

#define CAP_TABLE_MAX_SUBJECTS 8u

void cap_table_init(void);
void cap_table_reset(void);
cap_result_t cap_table_grant(cap_subject_id_t subject_id, capability_id_t capability_id);
cap_result_t cap_table_revoke(cap_subject_id_t subject_id, capability_id_t capability_id);
cap_result_t cap_table_check(cap_subject_id_t subject_id, capability_id_t capability_id);

#endif