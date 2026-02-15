# SecureOS Plan — M1 CAP-003 Gate First Privileged Operation Behind Capability Check

_Date: 2026-02-14_

## Why this is next

CAP-002 established deterministic per-subject grants with deny-by-default behavior. The next smallest zero-trust step is to enforce that contract on a concrete privileged path so unauthorized subjects are denied in execution, not just in table APIs.

## Goal

Implement CAP-003 as a focused slice: gate the first privileged operation (console write path) behind `cap_check` and validate both deny and allow outcomes with deterministic test markers.

## Scope

### In scope

1. Introduce a minimal privileged console operation wrapper that requires `CAP_CONSOLE_WRITE`.
2. Enforce deny-by-default before any privileged side effect occurs.
3. Add deterministic test target `capability_gate` covering:
   - default deny
   - explicit allow after grant
   - deny restored after revoke
   - invalid subject handling
4. Update architecture/planning docs with CAP-003 behavior and validation command.

### Out of scope

- Scheduler-integrated subject identity lifecycle
- Manifest/broker policy distribution
- Multi-capability policy composition
- CAP-004 harness-wide marker integration across QEMU flows

## Zero-trust invariants

- No privileged console operation executes without explicit capability grant.
- Denied checks must surface explicit `cap_result_t` errors.
- Revoke takes effect immediately for subsequent privileged attempts.
- Invalid subjects are rejected as explicit errors.

## Implementation tasks (single small PR)

1. Add `kernel/cap/cap_gate.h` + `kernel/cap/cap_gate.c` for the privileged console gate API.
2. Implement gate function that checks `cap_check(subject, CAP_CONSOLE_WRITE)` before success path.
3. Add `tests/capability_gate_test.c` with deterministic marker output:
   - `TEST:START:capability_gate`
   - `TEST:PASS:capability_gate_default_deny`
   - `TEST:PASS:capability_gate_allow_after_grant`
   - `TEST:PASS:capability_gate_revoke_restores_deny`
   - `TEST:PASS:capability_gate_invalid_subject`
   - failure format: `TEST:FAIL:capability_gate:<reason>`
4. Add `build/scripts/test_capability_gate.sh` and wire into `build/scripts/test.sh`.
5. Update `docs/architecture/CAPABILITIES.md` and `docs/planning/M0-M1-task-registry.md` status notes.

## Acceptance criteria

- `./build/scripts/test.sh capability_gate` exits 0.
- Test markers prove deny-by-default, allow after grant, revoke restore deny, and invalid-subject rejection.
- Existing capability tests remain green:
  - `./build/scripts/test.sh cap_api_contract`
  - `./build/scripts/test.sh capability_table`
- CI passes for the implementation PR.

## Risks and mitigations

- **Risk:** gate abstraction couples too tightly to one capability.
  - **Mitigation:** keep API surface capability-focused and minimal, with explicit constants.
- **Risk:** privileged success path could leak side effects on deny.
  - **Mitigation:** perform capability check first and assert no write-count mutation on denied path in tests.

## Done definition

CAP-003 is done when one small PR merges with green CI and deterministic evidence that the first privileged console operation is enforced by capability checks with default-deny semantics.