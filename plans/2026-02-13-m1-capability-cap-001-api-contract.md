# SecureOS Plan — M1 CAP-001 Capability API Contract (Zero-Trust by Default)

_Date: 2026-02-13_

## Why this is next

The repo now has deterministic boot validation and structured marker parsing. The next smallest, high-confidence step for zero-trust-by-default is the **first enforceable capability contract**: stable capability IDs, explicit check outcomes, and deterministic deny-path behavior.

This slices the broader M1 capability bootstrap into one mergeable unit with clear verification.

## Goal

Land CAP-001 as a focused vertical slice that introduces capability identity + check semantics without coupling to full process management.

## Scope

### In scope

1. Add kernel capability API surface:
   - `kernel/cap/capability.h`
   - `kernel/cap/capability.c`
2. Define stable capability IDs with room for future extension:
   - `CAP_CONSOLE_WRITE` (initial)
   - reserved ranges documented in header comments
3. Define explicit check result model:
   - `CAP_OK`
   - `CAP_ERR_MISSING`
   - `CAP_ERR_SUBJECT_INVALID`
   - `CAP_ERR_CAP_INVALID`
4. Add a deterministic boot-time/kernel test target validating contract behavior.
5. Emit structured harness markers for pass/fail.
6. Document invariants and extension rules in architecture docs.

### Out of scope

- Delegation broker
- Manifest signature verification
- Dynamic policy loading
- Multi-service enforcement rollout

## Zero-trust invariants (must hold)

- Default path denies unless an explicit grant exists.
- Every denied check yields a non-ambiguous explicit error code.
- Capability IDs are stable and never renumbered after merge.
- Validation includes both allow and deny evidence.

## Implementation tasks (single small PR)

### 1) Capability API + model

- Introduce `capability_id_t` and `cap_result_t` types.
- Add `cap_check(subject_id, capability_id)` contract.
- Add clear parameter validation path for invalid subject/cap IDs.

### 2) Minimal subject grant storage for CAP-001 tests

- Implement fixed, in-memory grant table sized for bootstrap tests.
- Ensure zero-init/default deny semantics.
- Add kernel-only test helper for deterministic grant setup.

### 3) Deterministic validation target

- Extend test harness with `cap_api_contract` target.
- Emit markers:
  - `TEST:START:cap_api_contract`
  - `TEST:PASS:cap_api_contract_default_deny`
  - `TEST:PASS:cap_api_contract_allow`
  - `TEST:PASS:cap_api_contract_invalid_inputs`
  - failure marker form: `TEST:FAIL:cap_api_contract:<reason>`

### 4) Docs

- Add `docs/architecture/CAPABILITIES.md` (or extend if present) with:
  - capability ID stability rule
  - deny-by-default rule
  - error code semantics
  - CAP-002/CAP-003 follow-on notes

## Acceptance criteria

- `./build/scripts/test.sh cap_api_contract` exits 0.
- Structured markers for all expected pass checks are present.
- Invalid subject/capability inputs return explicit non-generic errors.
- No implicit grant path exists for newly initialized subjects.
- CI job for the PR is green.

## Risks and mitigations

- **Risk:** early API overfits future scheduler/process model.
  - **Mitigation:** keep subject handle opaque and small; defer policy plumbing.
- **Risk:** unstable enum/ID naming causes churn.
  - **Mitigation:** document and enforce append-only ID policy now.
- **Risk:** tests validate only happy path.
  - **Mitigation:** require explicit invalid-input and default-deny pass markers.

## Done definition

This plan is done when one PR implementing CAP-001 merges with green CI and deterministic pass markers proving deny-by-default + explicit error handling.