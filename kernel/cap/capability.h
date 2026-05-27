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
  CAP_IPC_SEND = 13,
  CAP_IPC_RECV = 14,
  /*
   * CAP_SYSCALL: reserved-but-unused in M1 (issue #232).
   *
   * Gates the M1 syscall-entry ABI vector (see
   * `kernel/proc/syscall_entry.{c,h}` and `docs/abi/syscalls.md`).
   * No call sites exist yet — `kernel_syscall_entry` returns
   * `IPC_ERR_INVALID_MSG` for every vector in M1. The capability is
   * declared now so the deny-marker contract
   * (`docs/abi/capability-deny-contract.md` §4) has a single source of
   * truth for the marker's <capability_id> field the moment any real
   * caller is wired in M2+.
   */
  CAP_SYSCALL = 15,
  CAP_CLOCK_SET = 16,
  /*
   * CAP_INPUT_MOUSE / CAP_INPUT_KEYBOARD / CAP_GFX_FRAMEBUFFER:
   * zero-trust gate registry entries for the M5/M6 graphics + input
   * surfaces introduced by the merged window-manager + virtual-graphics
   * + PS/2 mouse/keyboard stack (PRs #321/#322/#328/#334/#340).
   *
   * Tracking: issue #348. The HAL/driver enforcement follow-up is
   * tracked separately (#349); these ids exist now so the registry,
   * deny-marker grammar, and manifest schema have stable numeric
   * anchors before the M6 SDK freeze (BUILD_ROADMAP §5.6 / §7).
   *
   * `CAP_INPUT_MOUSE` is the rename of the original `CAP_MOUSE` slot
   * (#340) into the canonical `input.*` naming. The numeric id is
   * unchanged at 17 — the append-only contract pins numbers, not
   * symbolic names. No call site emitted a `CAP:DENY:mouse:` marker
   * before this rename, so no on-wire string drifts.
   */
  CAP_INPUT_MOUSE = 17,
  CAP_GFX_FRAMEBUFFER = 18,
  CAP_INPUT_KEYBOARD = 19,
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
  /*
   * Issue #370: per-child cascade revoke emitted by
   * broker_svc_delete_owner. Schema-compatible with the existing
   * cap_audit_event_t tuple (#98) — `actor_subject_id` is the
   * caller of broker_svc_delete_owner, `subject_id` is the affected
   * recipient / session-owner subject, `capability_id` is the
   * underlying CAP_* the share carried (or 0 for non-cap edges such
   * as the step-3.5 session teardown).
   */
  CAP_AUDIT_OP_CASCADE_REVOKE = 3,
  /*
   * Issue #370: terminal cap.cascade.done event emitted exactly once
   * per broker_svc_delete_owner ALLOW invocation. `subject_id` is
   * the owner whose tree was torn down; `capability_id` carries the
   * `n_children` cascade count (overloaded — fits in v0 because
   * capability_id is u32 and the bound is BROKER_SVC_CASCADE_*).
   */
  CAP_AUDIT_OP_CASCADE_DONE = 4,
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

typedef enum {
  CAP_AUDIT_OUTCOME_ALLOW = 0,
  CAP_AUDIT_OUTCOME_DENY = 1,
  CAP_AUDIT_OUTCOME_GRANTED = 2,
  CAP_AUDIT_OUTCOME_GRANT_DENIED = 3,
  CAP_AUDIT_OUTCOME_REVOKED = 4,
  CAP_AUDIT_OUTCOME_REVOKE_DENIED = 5,
  /* Issue #370: outcomes for the cascade ops above. */
  CAP_AUDIT_OUTCOME_CASCADE_REVOKED = 6,
  CAP_AUDIT_OUTCOME_CASCADE_DONE    = 7,
} cap_audit_outcome_t;

/*
 * Stable, serial-first textual serialization for capability audit events.
 *
 * Lines are deterministic and human/machine readable, e.g.:
 *   CAP_AUDIT:seq=3:op=CHECK:actor=1:subject=1:cap=1:result=MISSING:outcome=DENY
 *
 * The formatter is a pure function of its inputs and must not mutate the
 * audit ring, the checkpoint state, or any capability table state. This is
 * the non-interference contract enforced by the audit-log tests.
 */
cap_audit_outcome_t cap_audit_event_outcome(const cap_audit_event_t *event);

/*
 * Format a single audit event into `buf` (NUL-terminated on success). Returns
 * the number of bytes written excluding the terminator on success, or a
 * negative value on error (NULL inputs, zero-size buffer, truncation).
 */
int cap_audit_format_event(const cap_audit_event_t *event,
                           char *buf,
                           size_t buf_size);

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

/*
 * Public audit-ring emitter.
 *
 * Records a structured audit event for a capability-mediated decision made
 * outside `capability.c` itself (e.g. broker grant/deny/revoke, fs_svc
 * persistent-write deny). This is the canonical wiring path closed by issue
 * #311: callers that previously had no way to publish into the audit ring
 * now share the same `cap_audit_event_t` tuple used by `cap_check` /
 * `cap_grant_as_for_tests` / `cap_revoke_as_for_tests`.
 *
 * No-ABI-bump: this is an additive in-tree symbol; the on-wire and on-disk
 * audit record schema is unchanged.
 */
void cap_audit_emit(cap_audit_op_t operation,
                    cap_subject_id_t actor_subject_id,
                    cap_subject_id_t subject_id,
                    capability_id_t capability_id,
                    cap_result_t result);

void cap_audit_reset_for_tests(void);
size_t cap_audit_count_for_tests(void);
size_t cap_audit_dropped_for_tests(void);
cap_result_t cap_audit_get_for_tests(size_t index, cap_audit_event_t *out_event);
size_t cap_audit_checkpoint_count_for_tests(void);
cap_result_t cap_audit_checkpoint_get_for_tests(size_t index,
                                                cap_audit_checkpoint_t *out_checkpoint);

#endif
