# 2026-04-08 Zero-Trust Workflow Rule Hardening

## Goal
Tighten the workflow rule layer so every rule evaluation is tenant-scoped,
explicitly scoped for read or write, and deny-by-default. This is the planning
companion to issue #77 and predates the network ABI hardening slice
(plan 2026-04-09) so the broader zero-trust posture stays consistent across
subsystems.

## Scope
- Persisted workflow rules are bound to a tenant identifier and never evaluated
  outside of that tenant.
- Every rule declares an explicit read or write scope; missing scope means
  deny.
- Evaluation helpers fail closed when any required field is missing,
  ambiguous, or cross-tenant.
- Existing console, capability, and audit semantics stay unchanged; this slice
  only narrows the workflow rule surface.
- Validation lands as small, deterministic tests focused on tenant isolation
  and scope enforcement.

## Plan

### Phase 1, rule shape
- Define the minimal persisted workflow rule record:
  - tenant id (required, non-zero).
  - rule id (stable, unique per tenant).
  - scope (`READ` or `WRITE`, no implicit `READ_WRITE`).
  - subject + capability the rule applies to.
  - reason code for audit, no free-form text.
- Reject rule construction when any required field is missing or zero.
- Keep the schema small enough to be embedded in existing capability metadata
  without introducing a new persistence layer in this slice.

### Phase 2, evaluation helpers
- Add `workflow_rule_eval_read` and `workflow_rule_eval_write` helpers that:
  - Take the calling tenant id and the rule id.
  - Look up the rule in the caller's tenant only; cross-tenant lookups return
    `CAP_ERR_MISSING` without leaking existence.
  - Require the requested scope to match the rule's declared scope; mismatch
    returns `CAP_ERR_MISSING`, not a softer error.
  - Default to deny when the rule is absent, malformed, or scope-mismatched.
- Provide a single `workflow_rule_reset_for_tests` helper to keep tests
  deterministic and serial-first.

### Phase 3, audit + non-interference
- Route allow and deny decisions into the existing capability audit ring with
  a dedicated `CAP_AUDIT_OP_WORKFLOW_*` operation tag.
- Logging must not change the deny result and must not widen access on the
  allow path.
- Keep audit volume bounded: one record per evaluation, no hot-path
  amplification.

### Phase 4, validation
- Add one allow-path test: same tenant, matching scope, expected `CAP_OK`.
- Add one deny-path test: cross-tenant lookup returns `CAP_ERR_MISSING` and is
  recorded as a deny in audit.
- Add one scope-mismatch test: read request against a write-only rule (and
  vice versa) returns `CAP_ERR_MISSING`.
- Add one regression test proving audit logging does not flip a deny into an
  allow.

## Exit Criteria
- Workflow rules are tenant-scoped end to end; no ambient rule lookups.
- Read and write scopes are explicit, with no implicit widening.
- Default-deny holds for missing, malformed, or mismatched rules.
- Tenant-isolation, scope-mismatch, and non-interference tests pass
  deterministically under `build/scripts/test.sh`.
- The slice stays small enough to land in one PR without touching the console,
  filesystem, or network slices.

## Follow-ups (out of scope for this plan)
- Persist rules across reboots once the filesystem service slice
  (plan 2026-04-16) is wired in.
- Surface workflow decisions through the capability broker share workflow
  (plan 2026-04-21) once that slice lands.
