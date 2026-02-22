# Plan — M1-CAP-012: Capability Audit Summary Schema Guardrail

## Why this next

CAP-011 established an explicit audit-ring overflow contract and emits a CI-facing audit summary artifact. The next smallest zero-trust increment is to make that artifact **strictly validated** in the standard validation flow so schema drift or missing fields fail fast in CI.

## Scope

Implement a deterministic guardrail for `artifacts/tests/capability_audit_summary.json`:

1. Require the summary artifact to exist whenever `capability_audit` runs.
2. Validate required fields and types:
   - `schemaVersion` (integer, currently `1`)
   - `test` (`"capability_audit"`)
   - `ringCapacity` (integer > 0)
   - `retainedEvents` (integer >= 0)
   - `droppedEvents` (integer >= 0)
3. Enforce contract invariants:
   - `retainedEvents <= ringCapacity`
   - `retainedEvents + droppedEvents >= ringCapacity` for overflow scenarios observed by the suite
4. Surface guardrail result in validator output so CI summaries can consume pass/fail deterministically.

## Non-goals

- Expanding capability IDs or adding new privileged operations.
- Redesigning audit ring storage.
- Introducing external schema tooling dependencies.

## Implementation slices

### Slice A — Validator contract check
- Add a Python validator step in `build/scripts/validate_bundle.sh` that loads `capability_audit_summary.json` and enforces required keys/types/invariants.
- Fail validation with explicit error messages on missing/invalid fields.

### Slice B — CI visibility
- Include an explicit check record (e.g., `capability_audit_summary_contract`) in `validator_report.json` so failures are visible in machine-readable reports.

### Slice C — Docs/state sync
- Update `docs/planning/M0-M1-task-registry.md` with M1-CAP-012 row as `in_progress` when issue opens, then `done` when merged.
- Add a short note to weekly review once merged summarizing the guardrail behavior.

## Acceptance criteria

- `./build/scripts/validate_bundle.sh` fails if audit summary artifact is missing.
- `./build/scripts/validate_bundle.sh` fails if any required field is missing/wrong type/out of bounds.
- Validator report contains explicit pass/fail status for the summary contract check.
- Existing suite still passes with valid artifact data.

## Verification commands

- `./build/scripts/test.sh capability_audit`
- `./build/scripts/validate_bundle.sh`

## Risks and mitigations

- **Risk:** False positives from overly strict invariants.
  - **Mitigation:** Keep invariants tied to current deterministic test behavior and document rationale inline.
- **Risk:** Silent future schema drift.
  - **Mitigation:** Centralize required field checks in validator and emit named check status in report.
