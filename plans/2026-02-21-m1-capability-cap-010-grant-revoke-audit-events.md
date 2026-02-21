# 2026-02-21 — M1-CAP-010 grant/revoke audit events

## Goal
Extend capability audit visibility to include **state mutation operations** (`grant` and `revoke`) while preserving zero-trust-by-default behavior.

Today, `cap_check(...)` writes deterministic audit events, but permission table mutations are silent. This plan adds deterministic mutation auditing so test harnesses can prove *who was granted/revoked what* and with what result code.

## Zero-trust alignment
- No implicit grants introduced.
- Invalid subject/capability writes remain explicit errors.
- Audit is observability-only; policy enforcement paths remain unchanged.

## Scope (incremental)
1. Add operation type to `cap_audit_event_t`:
   - `CAP_AUDIT_OP_CHECK`
   - `CAP_AUDIT_OP_GRANT`
   - `CAP_AUDIT_OP_REVOKE`
2. Route `cap_grant_for_tests(...)` and `cap_revoke_for_tests(...)` through audit recording wrappers.
3. Keep ring-buffer semantics unchanged (bounded FIFO with deterministic overwrite behavior).
4. Extend audit tests to validate op type + ordering for mixed check/grant/revoke flows.
5. Update architecture docs + task registry status entry for `M1-CAP-010`.

## Files expected
- `kernel/cap/capability.h`
- `kernel/cap/capability.c`
- `tests/capability_audit_test.c`
- `docs/architecture/CAPABILITIES.md`
- `docs/planning/M0-M1-task-registry.md`

## Validation
- `./build/scripts/test.sh capability_audit`

## Definition of done
- Mutation operations emit deterministic audit events with explicit operation type.
- Existing check-audit behavior remains intact.
- Validation command passes locally and in PR CI.
- Registry marks `M1-CAP-010` as `done` only after merge.

## Out of scope
- Runtime/userland audit export channel.
- Persistent audit storage.
- New capabilities or subject model changes.

## Risks + mitigations
- **Risk:** event ordering regressions.
  - **Mitigation:** explicit mixed-flow ordering assertions in tests.
- **Risk:** accidental API break for existing audit tests.
  - **Mitigation:** update tests atomically with struct changes.
