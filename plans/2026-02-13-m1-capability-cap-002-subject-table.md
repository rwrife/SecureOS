# SecureOS Plan — M1 CAP-002 Per-Subject Capability Table (Deny-by-Default)

_Date: 2026-02-13_

## Why this is next

CAP-001 defines capability IDs and check-result semantics. The next smallest enforceable zero-trust step is a deterministic per-subject grant table that stays deny-by-default unless explicitly configured.

## Goal

Implement CAP-002 as a focused slice: a fixed-size subject capability table with explicit grant/revoke APIs and deterministic validation markers proving default-deny and targeted allow behavior.

## Scope

### In scope

1. Add `kernel/cap/cap_table.h` + `kernel/cap/cap_table.c` with fixed-capacity subject entries.
2. Keep zero-init/default-deny semantics for all unconfigured subjects.
3. Add explicit API operations:
   - init/reset table
   - grant capability to subject
   - revoke capability from subject
   - check capability for subject
4. Add deterministic validation target `capability_table` and machine-readable markers.
5. Update architecture/planning docs with capacity limits + extension path.

### Out of scope

- Dynamic subject allocation
- Persistence or policy manifests
- Delegation chains and cross-service trust propagation
- Full privileged-path gate integration (CAP-003)

## Zero-trust invariants

- No subject has capabilities unless explicitly granted.
- Unknown/invalid subjects are denied with explicit error code.
- Grant state is deterministic and resettable for test reproducibility.
- Revoke must restore deny behavior immediately.

## Implementation tasks (single small PR)

1. Define compact table model (`MAX_SUBJECTS`, bitset/array-backed grants).
2. Implement bounds-checked grant/revoke/check APIs with explicit result codes.
3. Add deterministic host/kernel-adjacent validation executable for `./build/scripts/test.sh capability_table`.
4. Emit markers:
   - `TEST:START:capability_table`
   - `TEST:PASS:capability_table_default_deny`
   - `TEST:PASS:capability_table_grant_allow`
   - `TEST:PASS:capability_table_revoke_deny`
   - `TEST:PASS:capability_table_invalid_inputs`
   - failure format: `TEST:FAIL:capability_table:<reason>`
5. Document fixed-capacity constraints and CAP-003 follow-on in docs.

## Acceptance criteria

- `./build/scripts/test.sh capability_table` exits 0.
- Marker evidence includes default-deny, allow, revoke, and invalid-input checks.
- Table starts deny-by-default across all subjects after init/reset.
- CI is green for the PR.

## Risks and mitigations

- **Risk:** table model blocks later scheduler integration.
  - **Mitigation:** keep subject identifier opaque and table internals encapsulated.
- **Risk:** accidental implicit grants via stale state.
  - **Mitigation:** mandatory explicit init/reset in tests and deterministic validation checks.

## Done definition

CAP-002 is done when one small PR merges with green CI and deterministic markers proving deny-by-default table behavior with explicit grant/revoke transitions.
