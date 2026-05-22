#ifndef SECUREOS_WORKFLOW_RULE_H
#define SECUREOS_WORKFLOW_RULE_H

/**
 * @file workflow_rule.h
 * @brief Tenant-scoped, scope-explicit workflow rule layer (issue #77).
 *
 * Purpose:
 *   Minimal persisted workflow-rule table whose evaluation is:
 *     - tenant-scoped end-to-end (no ambient or cross-tenant lookups),
 *     - explicit per-direction (READ vs WRITE, no implicit READ_WRITE),
 *     - deny-by-default for missing, malformed, or scope-mismatched rules.
 *
 *   This is the planning companion to plan
 *   `plans/2026-04-08-zero-trust-workflow-rule-hardening.md`. It is
 *   deliberately a small, self-contained slice: it does not change
 *   console, capability-table, or audit-ring semantics, and ships its
 *   own deterministic, serial-first audit hook for tests and CI.
 *
 * Used by:
 *   - kernel/cap/workflow_rule.c (implementation),
 *   - tests/workflow_rule_test.c (allow/deny/scope-mismatch coverage),
 *   - build/scripts/test_workflow_rule.sh dispatcher.
 *
 * Not used by (out of scope for this slice):
 *   - persistence layer (filesystem service) — see plan 2026-04-16,
 *   - capability broker share workflow — see plan 2026-04-21.
 */

#include <stddef.h>
#include <stdint.h>

#include "capability.h"

/*
 * Sizing.
 *
 * Kept intentionally small. The slice is about correctness of the
 * tenant/scope discipline, not capacity. Persistence/scale follow-ups
 * are tracked in the plan doc.
 */
#define WORKFLOW_RULE_MAX_RULES 16u

/*
 * Reserved tenant id. Zero is *never* a valid tenant id — rule
 * construction with tenant_id == 0 must fail closed. This keeps
 * "uninitialized" memory from accidentally satisfying a tenant check.
 */
#define WORKFLOW_RULE_TENANT_INVALID ((workflow_tenant_id_t)0u)

/*
 * Reserved rule id. Zero is *never* a valid rule id. Same fail-closed
 * rationale as the tenant id above.
 */
#define WORKFLOW_RULE_ID_INVALID ((workflow_rule_id_t)0u)

typedef uint32_t workflow_tenant_id_t;
typedef uint32_t workflow_rule_id_t;

/*
 * Explicit, single-direction scope. There is *no* READ_WRITE value:
 * the plan calls out that missing or implicit widening must deny.
 * Callers that need both directions must register two rules.
 */
typedef enum {
  WORKFLOW_SCOPE_NONE  = 0,
  WORKFLOW_SCOPE_READ  = 1,
  WORKFLOW_SCOPE_WRITE = 2,
} workflow_scope_t;

/*
 * Reason code attached to every rule for audit. Free-form text is
 * intentionally not supported on the rule (the plan calls this out
 * explicitly) to keep audit volume bounded and machine-comparable.
 */
typedef enum {
  WORKFLOW_REASON_NONE          = 0,
  WORKFLOW_REASON_TENANT_POLICY = 1,
  WORKFLOW_REASON_OPERATOR      = 2,
  WORKFLOW_REASON_AUTOMATION    = 3,
} workflow_reason_t;

typedef enum {
  WORKFLOW_OK                          = 0,
  WORKFLOW_ERR_MISSING                 = 1, /* deny: no rule, wrong tenant, scope mismatch */
  WORKFLOW_ERR_TENANT_INVALID          = 2,
  WORKFLOW_ERR_RULE_INVALID            = 3,
  WORKFLOW_ERR_SCOPE_INVALID           = 4,
  WORKFLOW_ERR_SUBJECT_INVALID         = 5,
  WORKFLOW_ERR_CAPABILITY_INVALID      = 6,
  WORKFLOW_ERR_REASON_INVALID          = 7,
  WORKFLOW_ERR_DUPLICATE               = 8,
  WORKFLOW_ERR_NO_SLOT                 = 9,
} workflow_result_t;

/*
 * Audit event recorded once per evaluation. The non-interference
 * contract is the same as the capability audit ring: formatting these
 * events must never mutate evaluation state, and recording them must
 * never flip an allow into a deny (or vice versa).
 */
typedef enum {
  WORKFLOW_AUDIT_OP_REGISTER = 0,
  WORKFLOW_AUDIT_OP_EVAL_READ  = 1,
  WORKFLOW_AUDIT_OP_EVAL_WRITE = 2,
} workflow_audit_op_t;

typedef enum {
  WORKFLOW_AUDIT_OUTCOME_ALLOW = 0,
  WORKFLOW_AUDIT_OUTCOME_DENY  = 1,
} workflow_audit_outcome_t;

typedef struct {
  uint64_t                  sequence_id;
  workflow_audit_op_t       operation;
  workflow_tenant_id_t      tenant_id;
  workflow_rule_id_t        rule_id;
  workflow_scope_t          scope_requested;
  cap_subject_id_t          subject_id;
  capability_id_t           capability_id;
  workflow_reason_t         reason;
  workflow_result_t         result;
  workflow_audit_outcome_t  outcome;
} workflow_audit_event_t;

#define WORKFLOW_AUDIT_EVENT_MAX 32u

/*
 * Lifecycle.
 *
 * Reset is the single test-only helper. The plan called this out as
 * the deterministic seam for serial-first tests; production code paths
 * never call it (the kernel boot path will, exactly once, when
 * eventually wired up).
 */
void workflow_rule_reset_for_tests(void);

/*
 * Register a persisted rule. The rule is bound to its tenant for life
 * and never visible from another tenant. Duplicate (tenant, rule_id)
 * fails closed with WORKFLOW_ERR_DUPLICATE rather than overwriting,
 * so writers must explicitly reset before re-registering.
 */
workflow_result_t workflow_rule_register(workflow_tenant_id_t tenant_id,
                                         workflow_rule_id_t   rule_id,
                                         workflow_scope_t     scope,
                                         cap_subject_id_t     subject_id,
                                         capability_id_t      capability_id,
                                         workflow_reason_t    reason);

/*
 * Read-direction evaluator. Returns WORKFLOW_OK only when:
 *   - tenant_id is valid and matches the rule's tenant,
 *   - rule_id is valid and exists within that tenant,
 *   - the rule's declared scope is exactly WORKFLOW_SCOPE_READ,
 *   - subject_id and capability_id match the rule.
 * Any other condition returns WORKFLOW_ERR_MISSING (no existence
 * leak across tenants, no softer error for scope mismatch).
 */
workflow_result_t workflow_rule_eval_read(workflow_tenant_id_t caller_tenant_id,
                                          workflow_rule_id_t   rule_id,
                                          cap_subject_id_t     subject_id,
                                          capability_id_t      capability_id);

/*
 * Write-direction evaluator. Mirror of the read evaluator above; the
 * rule's scope must be exactly WORKFLOW_SCOPE_WRITE.
 */
workflow_result_t workflow_rule_eval_write(workflow_tenant_id_t caller_tenant_id,
                                           workflow_rule_id_t   rule_id,
                                           cap_subject_id_t     subject_id,
                                           capability_id_t      capability_id);

/*
 * Audit accessors. Read-only, non-mutating, bounded by the ring size.
 * The audit ring is independent from the capability audit ring so
 * this slice cannot regress existing audit-log invariants.
 */
size_t            workflow_audit_count_for_tests(void);
size_t            workflow_audit_dropped_for_tests(void);
workflow_result_t workflow_audit_get_for_tests(size_t index,
                                               workflow_audit_event_t *out_event);

/*
 * Stable, serial-first textual formatter for one event. Pure function
 * of its inputs; safe to call any number of times. Returns the number
 * of bytes written excluding the terminator, or a negative value on
 * truncation / bad input. Line shape:
 *
 *   WORKFLOW_AUDIT:seq=N:op=EVAL_READ:tenant=T:rule=R:scope=READ
 *                  :subject=S:cap=C:reason=TENANT_POLICY
 *                  :result=OK:outcome=ALLOW
 */
int workflow_audit_format_event(const workflow_audit_event_t *event,
                                char *buf,
                                size_t buf_size);

#endif /* SECUREOS_WORKFLOW_RULE_H */
