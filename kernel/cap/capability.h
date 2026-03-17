#ifndef SECUREOS_CAPABILITY_H
#define SECUREOS_CAPABILITY_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t cap_subject_id_t;

typedef enum {
  CAP_CONSOLE_WRITE = 1,
  CAP_SERIAL_WRITE = 2,
  CAP_DEBUG_EXIT = 3,
  CAP_CAPABILITY_ADMIN = 4,
  CAP_DISK_IO_REQUEST = 5,
  CAP_FS_READ = 6,
  CAP_FS_WRITE = 7,
  CAP_EVENT_SUBSCRIBE = 8,
  CAP_EVENT_PUBLISH = 9,
  CAP_APP_EXEC = 10,
  CAP_CODESIGN_BYPASS = 11,
  CAP_NETWORK = 12,
} capability_id_t;

typedef enum {
  CAP_OK = 0,
  CAP_ERR_MISSING = 1,
  CAP_ERR_SUBJECT_INVALID = 2,
  CAP_ERR_CAP_INVALID = 3,
} cap_result_t;

typedef enum {
  CAP_ACCESS_DENY = 0,
  CAP_ACCESS_ALLOW = 1,
  CAP_ACCESS_PENDING = 2,
} cap_access_state_t;

enum {
  CAP_AUDIT_EVENT_MAX = 32,
  CAP_AUDIT_CHECKPOINT_INTERVAL = 8,
  CAP_AUDIT_CHECKPOINT_MAX = 8,
};

typedef enum {
  CAP_AUDIT_OP_CHECK = 0,
  CAP_AUDIT_OP_GRANT = 1,
  CAP_AUDIT_OP_REVOKE = 2,
} cap_audit_op_t;

typedef struct {
  uint64_t sequence_id;
  cap_audit_op_t operation;
  cap_subject_id_t actor_subject_id;
  cap_subject_id_t subject_id;
  capability_id_t capability_id;
  cap_result_t result;
} cap_audit_event_t;

typedef struct {
  uint64_t checkpoint_id;
  uint64_t start_sequence_id;
  uint64_t end_sequence_id;
  uint64_t seal;
  size_t dropped_count;
} cap_audit_checkpoint_t;

void cap_reset_for_tests(void);
cap_result_t cap_grant_for_tests(cap_subject_id_t subject_id, capability_id_t capability_id);
cap_result_t cap_revoke_for_tests(cap_subject_id_t subject_id, capability_id_t capability_id);
cap_result_t cap_grant_as_for_tests(cap_subject_id_t actor_subject_id,
                                    cap_subject_id_t target_subject_id,
                                    capability_id_t capability_id);
cap_result_t cap_revoke_as_for_tests(cap_subject_id_t actor_subject_id,
                                     cap_subject_id_t target_subject_id,
                                     capability_id_t capability_id);
cap_result_t cap_check(cap_subject_id_t subject_id, capability_id_t capability_id);

void cap_audit_reset_for_tests(void);
size_t cap_audit_count_for_tests(void);
size_t cap_audit_dropped_for_tests(void);
cap_result_t cap_audit_get_for_tests(size_t index, cap_audit_event_t *out_event);
size_t cap_audit_checkpoint_count_for_tests(void);
cap_result_t cap_audit_checkpoint_get_for_tests(size_t index,
                                                cap_audit_checkpoint_t *out_checkpoint);

#endif
