/**
 * @file workflow_rule.c
 * @brief Tenant-scoped, scope-explicit workflow rule layer (issue #77).
 *
 * Implementation of the slice described in
 * `plans/2026-04-08-zero-trust-workflow-rule-hardening.md` and the
 * companion header `workflow_rule.h`.
 *
 * Design notes:
 *   - All state lives in this translation unit. No heap, no globals
 *     exposed across modules. The slice is intentionally self-contained
 *     so it cannot regress existing capability or audit semantics.
 *   - Evaluation is a single function (`workflow_rule_eval_locked`)
 *     parameterized by the requested scope; the public `_read` and
 *     `_write` wrappers exist solely to make scope explicit at the
 *     call site (no implicit READ_WRITE path).
 *   - Cross-tenant lookups, scope mismatches, and unknown rule ids
 *     all collapse to a single `WORKFLOW_ERR_MISSING` result so the
 *     existence of a rule does not leak between tenants.
 *   - Audit recording is non-interfering: the evaluator computes the
 *     result first, then records, then returns. A full audit ring
 *     drops the oldest record (bounded volume) but does not change
 *     the returned result.
 *
 * Used by:
 *   - tests/workflow_rule_test.c (allow/deny/scope-mismatch coverage),
 *   - build/scripts/test_workflow_rule.sh (validator dispatcher).
 *
 * Not yet used by:
 *   - kernel boot path / persistence (out of scope for this slice;
 *     tracked in the plan follow-ups).
 */

#include "workflow_rule.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  uint8_t              in_use;
  workflow_tenant_id_t tenant_id;
  workflow_rule_id_t   rule_id;
  workflow_scope_t     scope;
  cap_subject_id_t     subject_id;
  capability_id_t      capability_id;
  workflow_reason_t    reason;
} workflow_rule_slot_t;

static workflow_rule_slot_t   g_rules[WORKFLOW_RULE_MAX_RULES];
static workflow_audit_event_t g_audit_ring[WORKFLOW_AUDIT_EVENT_MAX];
static size_t                 g_audit_count;
static size_t                 g_audit_dropped;
static uint64_t               g_audit_next_sequence;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static int scope_is_directional(workflow_scope_t scope) {
  return scope == WORKFLOW_SCOPE_READ || scope == WORKFLOW_SCOPE_WRITE;
}

static int reason_is_valid(workflow_reason_t reason) {
  switch (reason) {
    case WORKFLOW_REASON_TENANT_POLICY:
    case WORKFLOW_REASON_OPERATOR:
    case WORKFLOW_REASON_AUTOMATION:
      return 1;
    case WORKFLOW_REASON_NONE:
    default:
      return 0;
  }
}

static int capability_is_known(capability_id_t cap) {
  switch (cap) {
    case CAP_CONSOLE_WRITE:
    case CAP_SERIAL_WRITE:
    case CAP_DEBUG_EXIT:
    case CAP_CAPABILITY_ADMIN:
    case CAP_DISK_IO_REQUEST:
    case CAP_FS_READ:
    case CAP_FS_WRITE:
    case CAP_EVENT_SUBSCRIBE:
    case CAP_EVENT_PUBLISH:
    case CAP_APP_EXEC:
    case CAP_CODESIGN_BYPASS:
    case CAP_NETWORK:
      return 1;
  }
  return 0;
}

static workflow_rule_slot_t *find_slot_for_tenant(workflow_tenant_id_t tenant_id,
                                                  workflow_rule_id_t   rule_id) {
  for (size_t i = 0; i < WORKFLOW_RULE_MAX_RULES; ++i) {
    workflow_rule_slot_t *slot = &g_rules[i];
    if (!slot->in_use) {
      continue;
    }
    if (slot->tenant_id == tenant_id && slot->rule_id == rule_id) {
      return slot;
    }
  }
  return NULL;
}

static workflow_rule_slot_t *find_free_slot(void) {
  for (size_t i = 0; i < WORKFLOW_RULE_MAX_RULES; ++i) {
    if (!g_rules[i].in_use) {
      return &g_rules[i];
    }
  }
  return NULL;
}

static workflow_audit_outcome_t result_to_outcome(workflow_result_t result) {
  return (result == WORKFLOW_OK) ? WORKFLOW_AUDIT_OUTCOME_ALLOW
                                 : WORKFLOW_AUDIT_OUTCOME_DENY;
}

static void audit_record(workflow_audit_op_t       operation,
                         workflow_tenant_id_t      tenant_id,
                         workflow_rule_id_t        rule_id,
                         workflow_scope_t          scope_requested,
                         cap_subject_id_t          subject_id,
                         capability_id_t           capability_id,
                         workflow_reason_t         reason,
                         workflow_result_t         result) {
  workflow_audit_event_t event;
  event.sequence_id     = g_audit_next_sequence++;
  event.operation       = operation;
  event.tenant_id       = tenant_id;
  event.rule_id         = rule_id;
  event.scope_requested = scope_requested;
  event.subject_id      = subject_id;
  event.capability_id   = capability_id;
  event.reason          = reason;
  event.result          = result;
  event.outcome         = result_to_outcome(result);

  if (g_audit_count < WORKFLOW_AUDIT_EVENT_MAX) {
    g_audit_ring[g_audit_count++] = event;
    return;
  }

  /* Ring is full. Drop the oldest, shift, append. Bounded volume,
   * non-interfering with the just-computed result. */
  for (size_t i = 1; i < WORKFLOW_AUDIT_EVENT_MAX; ++i) {
    g_audit_ring[i - 1] = g_audit_ring[i];
  }
  g_audit_ring[WORKFLOW_AUDIT_EVENT_MAX - 1] = event;
  g_audit_dropped++;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void workflow_rule_reset_for_tests(void) {
  memset(g_rules, 0, sizeof(g_rules));
  memset(g_audit_ring, 0, sizeof(g_audit_ring));
  g_audit_count = 0;
  g_audit_dropped = 0;
  g_audit_next_sequence = 0;
}

workflow_result_t workflow_rule_register(workflow_tenant_id_t tenant_id,
                                         workflow_rule_id_t   rule_id,
                                         workflow_scope_t     scope,
                                         cap_subject_id_t     subject_id,
                                         capability_id_t      capability_id,
                                         workflow_reason_t    reason) {
  if (tenant_id == WORKFLOW_RULE_TENANT_INVALID) {
    audit_record(WORKFLOW_AUDIT_OP_REGISTER, tenant_id, rule_id, scope,
                 subject_id, capability_id, reason,
                 WORKFLOW_ERR_TENANT_INVALID);
    return WORKFLOW_ERR_TENANT_INVALID;
  }
  if (rule_id == WORKFLOW_RULE_ID_INVALID) {
    audit_record(WORKFLOW_AUDIT_OP_REGISTER, tenant_id, rule_id, scope,
                 subject_id, capability_id, reason,
                 WORKFLOW_ERR_RULE_INVALID);
    return WORKFLOW_ERR_RULE_INVALID;
  }
  if (!scope_is_directional(scope)) {
    audit_record(WORKFLOW_AUDIT_OP_REGISTER, tenant_id, rule_id, scope,
                 subject_id, capability_id, reason,
                 WORKFLOW_ERR_SCOPE_INVALID);
    return WORKFLOW_ERR_SCOPE_INVALID;
  }
  if (subject_id == 0u) {
    audit_record(WORKFLOW_AUDIT_OP_REGISTER, tenant_id, rule_id, scope,
                 subject_id, capability_id, reason,
                 WORKFLOW_ERR_SUBJECT_INVALID);
    return WORKFLOW_ERR_SUBJECT_INVALID;
  }
  if (!capability_is_known(capability_id)) {
    audit_record(WORKFLOW_AUDIT_OP_REGISTER, tenant_id, rule_id, scope,
                 subject_id, capability_id, reason,
                 WORKFLOW_ERR_CAPABILITY_INVALID);
    return WORKFLOW_ERR_CAPABILITY_INVALID;
  }
  if (!reason_is_valid(reason)) {
    audit_record(WORKFLOW_AUDIT_OP_REGISTER, tenant_id, rule_id, scope,
                 subject_id, capability_id, reason,
                 WORKFLOW_ERR_REASON_INVALID);
    return WORKFLOW_ERR_REASON_INVALID;
  }

  if (find_slot_for_tenant(tenant_id, rule_id) != NULL) {
    audit_record(WORKFLOW_AUDIT_OP_REGISTER, tenant_id, rule_id, scope,
                 subject_id, capability_id, reason,
                 WORKFLOW_ERR_DUPLICATE);
    return WORKFLOW_ERR_DUPLICATE;
  }

  workflow_rule_slot_t *slot = find_free_slot();
  if (slot == NULL) {
    audit_record(WORKFLOW_AUDIT_OP_REGISTER, tenant_id, rule_id, scope,
                 subject_id, capability_id, reason,
                 WORKFLOW_ERR_NO_SLOT);
    return WORKFLOW_ERR_NO_SLOT;
  }

  slot->in_use        = 1u;
  slot->tenant_id     = tenant_id;
  slot->rule_id       = rule_id;
  slot->scope         = scope;
  slot->subject_id    = subject_id;
  slot->capability_id = capability_id;
  slot->reason        = reason;

  audit_record(WORKFLOW_AUDIT_OP_REGISTER, tenant_id, rule_id, scope,
               subject_id, capability_id, reason, WORKFLOW_OK);
  return WORKFLOW_OK;
}

static workflow_result_t eval_locked(workflow_audit_op_t  audit_op,
                                     workflow_scope_t     requested_scope,
                                     workflow_tenant_id_t caller_tenant_id,
                                     workflow_rule_id_t   rule_id,
                                     cap_subject_id_t     subject_id,
                                     capability_id_t      capability_id) {
  /* Fail-closed input checks. Note: we deliberately do *not* surface
   * distinct error codes for tenant-mismatch vs missing-rule vs
   * scope-mismatch on the evaluation path — they all collapse to
   * WORKFLOW_ERR_MISSING so existence does not leak across tenants. */

  workflow_result_t result;

  if (caller_tenant_id == WORKFLOW_RULE_TENANT_INVALID ||
      rule_id == WORKFLOW_RULE_ID_INVALID ||
      !scope_is_directional(requested_scope) ||
      subject_id == 0u ||
      !capability_is_known(capability_id)) {
    result = WORKFLOW_ERR_MISSING;
    audit_record(audit_op, caller_tenant_id, rule_id, requested_scope,
                 subject_id, capability_id, WORKFLOW_REASON_NONE, result);
    return result;
  }

  workflow_rule_slot_t *slot = find_slot_for_tenant(caller_tenant_id, rule_id);
  if (slot == NULL) {
    result = WORKFLOW_ERR_MISSING;
    audit_record(audit_op, caller_tenant_id, rule_id, requested_scope,
                 subject_id, capability_id, WORKFLOW_REASON_NONE, result);
    return result;
  }

  if (slot->scope != requested_scope ||
      slot->subject_id != subject_id ||
      slot->capability_id != capability_id) {
    result = WORKFLOW_ERR_MISSING;
    audit_record(audit_op, caller_tenant_id, rule_id, requested_scope,
                 subject_id, capability_id, slot->reason, result);
    return result;
  }

  result = WORKFLOW_OK;
  audit_record(audit_op, caller_tenant_id, rule_id, requested_scope,
               subject_id, capability_id, slot->reason, result);
  return result;
}

workflow_result_t workflow_rule_eval_read(workflow_tenant_id_t caller_tenant_id,
                                          workflow_rule_id_t   rule_id,
                                          cap_subject_id_t     subject_id,
                                          capability_id_t      capability_id) {
  return eval_locked(WORKFLOW_AUDIT_OP_EVAL_READ,
                     WORKFLOW_SCOPE_READ,
                     caller_tenant_id, rule_id, subject_id, capability_id);
}

workflow_result_t workflow_rule_eval_write(workflow_tenant_id_t caller_tenant_id,
                                           workflow_rule_id_t   rule_id,
                                           cap_subject_id_t     subject_id,
                                           capability_id_t      capability_id) {
  return eval_locked(WORKFLOW_AUDIT_OP_EVAL_WRITE,
                     WORKFLOW_SCOPE_WRITE,
                     caller_tenant_id, rule_id, subject_id, capability_id);
}

size_t workflow_audit_count_for_tests(void) {
  return g_audit_count;
}

size_t workflow_audit_dropped_for_tests(void) {
  return g_audit_dropped;
}

workflow_result_t workflow_audit_get_for_tests(size_t index,
                                               workflow_audit_event_t *out_event) {
  if (out_event == NULL) {
    return WORKFLOW_ERR_MISSING;
  }
  if (index >= g_audit_count) {
    return WORKFLOW_ERR_MISSING;
  }
  *out_event = g_audit_ring[index];
  return WORKFLOW_OK;
}

/* ------------------------------------------------------------------ */
/* Formatter                                                           */
/* ------------------------------------------------------------------ */

static const char *audit_op_name(workflow_audit_op_t op) {
  switch (op) {
    case WORKFLOW_AUDIT_OP_REGISTER:   return "REGISTER";
    case WORKFLOW_AUDIT_OP_EVAL_READ:  return "EVAL_READ";
    case WORKFLOW_AUDIT_OP_EVAL_WRITE: return "EVAL_WRITE";
  }
  return "UNKNOWN";
}

static const char *scope_name(workflow_scope_t scope) {
  switch (scope) {
    case WORKFLOW_SCOPE_NONE:  return "NONE";
    case WORKFLOW_SCOPE_READ:  return "READ";
    case WORKFLOW_SCOPE_WRITE: return "WRITE";
  }
  return "UNKNOWN";
}

static const char *reason_name(workflow_reason_t reason) {
  switch (reason) {
    case WORKFLOW_REASON_NONE:          return "NONE";
    case WORKFLOW_REASON_TENANT_POLICY: return "TENANT_POLICY";
    case WORKFLOW_REASON_OPERATOR:      return "OPERATOR";
    case WORKFLOW_REASON_AUTOMATION:    return "AUTOMATION";
  }
  return "UNKNOWN";
}

static const char *result_name(workflow_result_t result) {
  switch (result) {
    case WORKFLOW_OK:                     return "OK";
    case WORKFLOW_ERR_MISSING:            return "MISSING";
    case WORKFLOW_ERR_TENANT_INVALID:     return "TENANT_INVALID";
    case WORKFLOW_ERR_RULE_INVALID:       return "RULE_INVALID";
    case WORKFLOW_ERR_SCOPE_INVALID:      return "SCOPE_INVALID";
    case WORKFLOW_ERR_SUBJECT_INVALID:    return "SUBJECT_INVALID";
    case WORKFLOW_ERR_CAPABILITY_INVALID: return "CAPABILITY_INVALID";
    case WORKFLOW_ERR_REASON_INVALID:     return "REASON_INVALID";
    case WORKFLOW_ERR_DUPLICATE:          return "DUPLICATE";
    case WORKFLOW_ERR_NO_SLOT:            return "NO_SLOT";
  }
  return "UNKNOWN";
}

static const char *outcome_name(workflow_audit_outcome_t outcome) {
  switch (outcome) {
    case WORKFLOW_AUDIT_OUTCOME_ALLOW: return "ALLOW";
    case WORKFLOW_AUDIT_OUTCOME_DENY:  return "DENY";
  }
  return "UNKNOWN";
}

/* Tiny unsigned-to-decimal helper so the formatter has no libc deps
 * beyond <string.h>. Writes into `buf` at `*pos` and bumps `*pos`. */
static int u64_emit(char *buf, size_t buf_size, size_t *pos, uint64_t value) {
  char tmp[24];
  size_t n = 0;
  if (value == 0) {
    tmp[n++] = '0';
  } else {
    while (value > 0 && n < sizeof(tmp)) {
      tmp[n++] = (char)('0' + (value % 10u));
      value /= 10u;
    }
  }
  if (*pos + n >= buf_size) {
    return -1;
  }
  for (size_t i = 0; i < n; ++i) {
    buf[*pos + i] = tmp[n - 1 - i];
  }
  *pos += n;
  return 0;
}

static int str_emit(char *buf, size_t buf_size, size_t *pos, const char *s) {
  size_t n = strlen(s);
  if (*pos + n >= buf_size) {
    return -1;
  }
  memcpy(buf + *pos, s, n);
  *pos += n;
  return 0;
}

int workflow_audit_format_event(const workflow_audit_event_t *event,
                                char *buf,
                                size_t buf_size) {
  if (event == NULL || buf == NULL || buf_size == 0) {
    return -1;
  }
  size_t pos = 0;
  if (str_emit(buf, buf_size, &pos, "WORKFLOW_AUDIT:seq=") != 0) return -1;
  if (u64_emit(buf, buf_size, &pos, event->sequence_id) != 0) return -1;
  if (str_emit(buf, buf_size, &pos, ":op=") != 0) return -1;
  if (str_emit(buf, buf_size, &pos, audit_op_name(event->operation)) != 0) return -1;
  if (str_emit(buf, buf_size, &pos, ":tenant=") != 0) return -1;
  if (u64_emit(buf, buf_size, &pos, (uint64_t)event->tenant_id) != 0) return -1;
  if (str_emit(buf, buf_size, &pos, ":rule=") != 0) return -1;
  if (u64_emit(buf, buf_size, &pos, (uint64_t)event->rule_id) != 0) return -1;
  if (str_emit(buf, buf_size, &pos, ":scope=") != 0) return -1;
  if (str_emit(buf, buf_size, &pos, scope_name(event->scope_requested)) != 0) return -1;
  if (str_emit(buf, buf_size, &pos, ":subject=") != 0) return -1;
  if (u64_emit(buf, buf_size, &pos, (uint64_t)event->subject_id) != 0) return -1;
  if (str_emit(buf, buf_size, &pos, ":cap=") != 0) return -1;
  if (u64_emit(buf, buf_size, &pos, (uint64_t)event->capability_id) != 0) return -1;
  if (str_emit(buf, buf_size, &pos, ":reason=") != 0) return -1;
  if (str_emit(buf, buf_size, &pos, reason_name(event->reason)) != 0) return -1;
  if (str_emit(buf, buf_size, &pos, ":result=") != 0) return -1;
  if (str_emit(buf, buf_size, &pos, result_name(event->result)) != 0) return -1;
  if (str_emit(buf, buf_size, &pos, ":outcome=") != 0) return -1;
  if (str_emit(buf, buf_size, &pos, outcome_name(event->outcome)) != 0) return -1;

  buf[pos] = '\0';
  return (int)pos;
}
