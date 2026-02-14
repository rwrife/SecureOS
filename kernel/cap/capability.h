#ifndef SECUREOS_CAPABILITY_H
#define SECUREOS_CAPABILITY_H

#include <stdint.h>

typedef uint32_t cap_subject_id_t;

typedef enum {
  CAP_CONSOLE_WRITE = 1,
} capability_id_t;

typedef enum {
  CAP_OK = 0,
  CAP_ERR_MISSING = 1,
  CAP_ERR_SUBJECT_INVALID = 2,
  CAP_ERR_CAP_INVALID = 3,
} cap_result_t;

void cap_reset_for_tests(void);
cap_result_t cap_grant_for_tests(cap_subject_id_t subject_id, capability_id_t capability_id);
cap_result_t cap_check(cap_subject_id_t subject_id, capability_id_t capability_id);

#endif