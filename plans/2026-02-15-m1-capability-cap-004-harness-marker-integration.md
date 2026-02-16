# SecureOS Plan — M1 CAP-004 Capability Allow/Deny Markers Integrated in Harness

_Date: 2026-02-15_

## Why this is next

CAP-003 established an enforceable capability gate for the first privileged operation and introduced deterministic unit-level markers. The next incremental zero-trust step is to ensure those allow/deny outcomes are carried into the standard validation bundle so CI and downstream automation can consume them as machine-readable evidence.

## Goal

Complete CAP-004 by integrating `capability_gate` results into the validation harness/report path and by promoting task-registry state to reflect CAP-003 completion and CAP-004 execution.

## Scope

### In scope

1. Extend validation bundle execution to include `capability_gate` in the default check set.
2. Ensure `validator_report.json` explicitly includes `capability_gate` check status.
3. Add deterministic test evidence assertions that confirm capability gate markers are present in artifacts when bundle runs.
4. Update planning/docs status:
   - `M1-CAP-003` → `done`
   - `M1-CAP-004` → `in-progress` (or `done` if all acceptance criteria land in same PR)

### Out of scope

- New privileged operations beyond console write
- Subject identity lifecycle integration with scheduler/process model
- Policy broker/manifest distribution pipeline
- CAP-005 ADR expansion beyond minimal status updates

## Zero-trust invariants

- Deny-by-default remains enforced for privileged operation checks.
- Validation output must provide machine-readable proof of both allow and deny behavior.
- No implicit grants are introduced by harness integration.

## Implementation tasks (single small PR)

1. Update `build/scripts/validate_bundle.sh` test target set to include `capability_gate`.
2. Add/adjust tests to verify bundle report contains `capability_gate` with pass/fail semantics.
3. Confirm artifacts include capability-gate marker output under `artifacts/runs/<run-id>/tests`.
4. Update `docs/planning/M0-M1-task-registry.md` CAP status entries.

## Acceptance criteria

- `./build/scripts/validate_bundle.sh` exits 0 on green path and emits `VALIDATION_PASS:<path>`.
- `artifacts/runs/<run-id>/validator_report.json` includes a `checks[]` entry for `capability_gate`.
- `./build/scripts/test.sh capability_gate` remains green.
- CI passes for the implementation PR.

## Risks and mitigations

- **Risk:** Bundle grows brittle due to artifact path assumptions.
  - **Mitigation:** Keep assertions path-stable and bounded to known run artifact layout.
- **Risk:** Marker evidence not propagated into bundle artifacts.
  - **Mitigation:** Add explicit artifact copy/assert logic and fail fast on missing marker file.

## Done definition

CAP-004 is done when validation bundles consistently include machine-readable capability gate results and task registry status reflects the completed CAP-003 foundation with CAP-004 integrated and verified in CI.
