# Plan: M1-CAP-009 Capability Check Audit Log (Zero-Trust Visibility)

## Why this next

M1 capability enforcement is now active for console/serial/debug-exit, but there is no deterministic in-kernel audit surface proving *which* subject/capability checks were allowed or denied over time. For a zero-trust architecture, explicit deny-by-default decisions should be observable and testable.

## Scope

Add a bounded, deterministic capability-check audit log that records each authorization decision (`subject`, `capability`, `result`) and can be queried in tests.

## Non-goals

- No dynamic memory
- No persistence across boots
- No user-space ABI yet
- No behavior change to allow/deny decisions themselves

## Incremental execution steps

1. **Define audit contract in capability layer**
   - Introduce fixed-size ring buffer constants/types in `kernel/cap/capability.h`.
   - Add test-only accessors to reset and read audit events.

2. **Record every capability check**
   - Update `cap_check(...)` path to append one audit event per check result.
   - Preserve existing return semantics exactly.

3. **Add focused validation test**
   - New test target verifies:
     - default deny check is logged
     - grant+allow check is logged
     - invalid subject/capability checks are logged with explicit errors
     - bounded buffer behavior is deterministic

4. **Wire into test harness**
   - Add `build/scripts/test_capability_audit.sh`
   - Add `capability_audit` selector in `build/scripts/test.sh`

5. **Docs + registry update**
   - Update `docs/planning/M0-M1-task-registry.md` with `M1-CAP-009` as in-progress/done once merged.
   - Update `docs/architecture/CAPABILITIES.md` follow-on section with audit-log capability-check visibility.

## Validation command

- `./build/scripts/test.sh capability_audit`
- `./build/scripts/validate_bundle.sh`

## Done definition

- Capability decision audit records exist for allow/deny/invalid paths.
- Existing capability tests remain green.
- New audit test is deterministic and CI-friendly.
- Documentation and task registry reflect new task state.
