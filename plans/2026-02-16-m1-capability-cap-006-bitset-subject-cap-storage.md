# SecureOS Plan — M1 CAP-006 Bitset-backed Subject Capability Storage

_Date: 2026-02-16_

## Why this is next

CAP-005 finalized the capability-core ADR and locked zero-trust semantics for deny-by-default checks. The next incremental step is to make capability storage scalable without changing the API contract by replacing per-capability arrays with packed bitset storage per subject.

This keeps current behavior stable while preparing the kernel for additional capability IDs beyond `CAP_CONSOLE_WRITE`.

## Goal

Implement CAP-006 by migrating `cap_table` internals to packed bitsets (subject × capability bits), preserving existing check/grant/revoke semantics and deterministic test output.

## Scope

### In scope

1. Introduce bitset layout/constants in `kernel/cap/cap_table.c` for capability grants.
2. Migrate `cap_table_reset`, `cap_table_grant`, `cap_table_revoke`, and `cap_table_check` to bit operations.
3. Keep capability ID validation explicit and append-only safe.
4. Extend `tests/capability_table_test.c` with assertions that confirm:
   - default deny still holds across all subjects;
   - grant/revoke behavior remains isolated by subject;
   - multiple grants in one subject do not leak to others (future-safe structure test).
5. Update architecture/planning docs to mark CAP-006 status progression.

### Out of scope

- New privileged-operation gates beyond console write
- Dynamic subject lifecycle/scheduler identity integration
- Capability policy manifests or userspace broker
- ABI/API changes to public capability interfaces

## Zero-trust invariants

- Deny-by-default remains the baseline after init/reset.
- No implicit grants are introduced by storage migration.
- Invalid subject/capability IDs continue returning explicit errors.
- Existing capability IDs retain numeric stability.

## Implementation tasks (single small PR)

1. Replace single-capability grant array with compact per-subject bitset storage.
2. Add internal helper mapping from capability ID to bit index.
3. Keep `CAP_ID_MAX` enforcement and explicit invalid-cap rejection.
4. Update/expand table tests to verify unchanged externally visible behavior.
5. Update `docs/architecture/CAPABILITIES.md` follow-on section and task registry state.

## Acceptance criteria

- `./build/scripts/test.sh capability_table` passes.
- `./build/scripts/test.sh capability_gate` passes (regression guard).
- `./build/scripts/validate_bundle.sh` passes.
- CI passes on implementation PR.
- No change required in callers of `cap_table_*` APIs.

## Risks and mitigations

- **Risk:** Off-by-one mapping between capability IDs and bit positions.
  - **Mitigation:** Add explicit helper and boundary tests for min/max valid IDs.
- **Risk:** Silent behavior drift during migration.
  - **Mitigation:** Preserve existing test markers and run gate regression tests in same PR.

## Done definition

CAP-006 is done when bitset-backed storage is merged, tests confirm unchanged zero-trust behavior, and docs/registry reflect completion of the storage migration milestone.