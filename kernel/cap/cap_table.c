#include "cap_table.h"

#include <stdint.h>

#define CAP_ID_MAX CAP_CONSOLE_WRITE

static uint8_t subject_console_write_grant[CAP_TABLE_MAX_SUBJECTS];

static int cap_subject_valid(cap_subject_id_t subject_id) {
  return subject_id < CAP_TABLE_MAX_SUBJECTS;
}

static int cap_id_valid(capability_id_t capability_id) {
  return capability_id >= CAP_CONSOLE_WRITE && capability_id <= CAP_ID_MAX;
}

void cap_table_init(void) {
  cap_table_reset();
}

void cap_table_reset(void) {
  for (size_t i = 0; i < CAP_TABLE_MAX_SUBJECTS; ++i) {
    subject_console_write_grant[i] = 0;
  }
}

cap_result_t cap_table_grant(cap_subject_id_t subject_id, capability_id_t capability_id) {
  if (!cap_subject_valid(subject_id)) {
    return CAP_ERR_SUBJECT_INVALID;
  }

  if (!cap_id_valid(capability_id)) {
    return CAP_ERR_CAP_INVALID;
  }

  if (capability_id == CAP_CONSOLE_WRITE) {
    subject_console_write_grant[subject_id] = 1;
  }

  return CAP_OK;
}

cap_result_t cap_table_revoke(cap_subject_id_t subject_id, capability_id_t capability_id) {
  if (!cap_subject_valid(subject_id)) {
    return CAP_ERR_SUBJECT_INVALID;
  }

  if (!cap_id_valid(capability_id)) {
    return CAP_ERR_CAP_INVALID;
  }

  if (capability_id == CAP_CONSOLE_WRITE) {
    subject_console_write_grant[subject_id] = 0;
  }

  return CAP_OK;
}

cap_result_t cap_table_check(cap_subject_id_t subject_id, capability_id_t capability_id) {
  if (!cap_subject_valid(subject_id)) {
    return CAP_ERR_SUBJECT_INVALID;
  }

  if (!cap_id_valid(capability_id)) {
    return CAP_ERR_CAP_INVALID;
  }

  if (capability_id == CAP_CONSOLE_WRITE && subject_console_write_grant[subject_id] == 1) {
    return CAP_OK;
  }

  return CAP_ERR_MISSING;
}
