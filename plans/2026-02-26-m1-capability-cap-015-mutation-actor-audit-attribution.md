# SecureOS Plan — M1-CAP-015 Capability mutation actor attribution in audit events

## Why now
M1-CAP-014 tightened admin capability propagation to bootstrap root, but mutation audit events still only record the target subject. That obscures who attempted a grant/revoke and weakens forensic clarity when policy denials occur.

For zero-trust-by-default systems, mutation logs must attribute both **actor** and **target** so denied and successful changes are equally traceable.

## Scope (incremental)
1. Extend capability audit event schema to carry `actor_subject_id`.
2. Preserve existing check-event semantics by setting actor==subject for `CAP_AUDIT_OP_CHECK`.
3. For grant/revoke mutation paths (`cap_grant_as_for_tests`, `cap_revoke_as_for_tests`), write the explicit actor subject to audit records on both success and denial.
4. Keep ring buffer sizing/order behavior unchanged.

## Code targets
- `kernel/cap/capability.h`
  - add `actor_subject_id` to `cap_audit_event_t`.
- `kernel/cap/capability.c`
  - thread actor id through `cap_audit_record(...)`.
  - use explicit actor ids for all grant/revoke/check paths.
- `tests/capability_audit_test.c`
  - assert actor attribution for core, wrap, and mixed-overflow flows.
  - add focused grant/revoke-as actor-attribution assertions.
- `docs/planning/M0-M1-task-registry.md`
  - add `M1-CAP-015` row in `todo` (then advance to `in_progress`/`done` in implementation PR lifecycle).

## Validation
- `./build/scripts/test.sh capability_audit`
- `./build/scripts/test.sh cap_api_contract`
- `./build/scripts/validate_bundle.sh`

## Definition of done
- Every audit event includes actor identity.
- Mutation denials and successes are attributable to the acting subject.
- Check/grant/revoke ordering, retained count, and dropped count contracts remain deterministic.
- Validation commands above are green.

## Follow-up (out of scope)
- Include actor identity in machine-readable bundle summaries for downstream analytics.
- Add optional policy reason-code field for denied mutation attempts.
