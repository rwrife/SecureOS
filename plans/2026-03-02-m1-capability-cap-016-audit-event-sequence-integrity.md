# M1-CAP-016 Plan — Capability Audit Event Sequence Integrity

## Why this slice

CAP-015 added actor attribution to mutation audit events, but the bounded ring still exposes only relative ordering by array position. For deterministic forensics and CI assertions across wrap-around windows, each audit event should carry a monotonic sequence identifier so consumers can prove strict ordering even after overflow.

This preserves SecureOS zero-trust-by-default design by improving audit verifiability without weakening enforcement semantics.

## Scope (incremental)

1. Add a monotonic `sequence_id` field to `cap_audit_event_t`.
2. Track next sequence id in capability audit state and reset behavior.
3. Stamp each recorded check/grant/revoke event with a unique increasing sequence id.
4. Extend capability audit tests to assert:
   - strict monotonic sequence IDs in normal flows,
   - preserved monotonic sequence IDs across ring wrap,
   - no duplicate/regressive IDs in mixed-flow overflow scenarios.
5. Update architecture/planning docs to mark CAP-015 done and CAP-016 in progress.

## Non-goals

- No changes to capability allow/deny policy.
- No expansion of capability table size or subject model.
- No runtime persistence/export format changes beyond in-memory test contract.

## Files expected to change

- `kernel/cap/capability.h`
- `kernel/cap/capability.c`
- `tests/capability_audit_test.c`
- `docs/planning/M0-M1-task-registry.md`
- `docs/architecture/CAPABILITIES.md`

## Validation

- `./build/scripts/test.sh capability_audit`
- `./build/scripts/test.sh cap_api_contract`

## Done definition

- Audit events expose deterministic monotonic sequence IDs.
- Tests prove monotonic ordering survives ring wrap and mixed overflow.
- Docs reflect CAP-015 complete and CAP-016 current execution slice.
