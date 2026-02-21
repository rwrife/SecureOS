#ifndef SECUREOS_CAPABILITY_H
#define SECUREOS_CAPABILITY_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t cap_subject_id_t;

typedef enum {
  CAP_CONSOLE_WRITE = 1,
  CAP_SERIAL_WRITE = 2,
  CAP_DEBUG_EXIT = 3,
} capability_id_t;

typedef enum {
  CAP_OK = 0,
  CAP_ERR_MISSING = 1,
  CAP_ERR_SUBJECT_INVALID = 2,
  CAP_ERR_CAP_INVALID = 3,
} cap_result_t;

enum {
  CAP_AUDIT_EVENT_MAX = 32,
};

typedef enum {
  CAP_AUDIT_OP_CHECK = 0,
  CAP_AUDIT_OP_GRANT = 1,
  CAP_AUDIT_OP_REVOKE = 2,
} cap_audit_op_t;

typedef struct {
  cap_audit_op_t operation;
  cap_subject_id_t subject_id;
  capability_id_t capability_id;
  cap_result_t result;
} cap_audit_event_t;

void cap_reset_for_tests(void);
cap_result_t cap_grant_for_tests(cap_subject_id_t subject_id, capability_id_t capability_id);
cap_result_t cap_revoke_for_tests(cap_subject_id_t subject_id, capability_id_t capability_id);
cap_result_t cap_check(cap_subject_id_t subject_id, capability_id_t capability_id);

void cap_audit_reset_for_tests(void);
size_t cap_audit_count_for_tests(void);
cap_result_t cap_audit_get_for_tests(size_t index, cap_audit_event_t *out_event);

#endif
