# SecureOS Plan — M1 CAP-008 Debug-Exit Capability Gate + Harness Authorization Check

## Why this next

CAP-007 proved multi-capability isolation for console and serial writes, but QEMU debug-exit remains an implicitly trusted path in test flows. The next smallest zero-trust increment is to require an explicit capability for debug-exit signaling itself.

This closes an authority gap in the validation boundary: a subject should not be able to force pass/fail termination without a specific grant.

## Goal

Implement CAP-008 by introducing `CAP_DEBUG_EXIT` and a dedicated gate API that preserves deny-by-default semantics while keeping existing test harness behavior deterministic.

## Scope (incremental)

1. Extend capability IDs with append-only `CAP_DEBUG_EXIT = 3`.
2. Add `cap_debug_exit_gate(subject_id, code)` in `kernel/cap/cap_gate.{h,c}` with explicit capability checks.
3. Add focused tests in `tests/capability_gate_test.c` for:
   - default deny for debug-exit,
   - allow after grant,
   - revoke restores deny,
   - invalid subject handling,
   - no leakage from console/serial grants.
4. Extend `tests/capability_table_test.c` assertions to include default-deny coverage for `CAP_DEBUG_EXIT`.
5. Update capability docs and task registry state for CAP-008 progress.

## Non-goals

- Wiring real hardware reset/poweroff semantics.
- Userspace manifest delegation.
- Scheduler/process model changes.

## Zero-trust constraints

- No implicit debug-exit authority for any subject.
- Existing cap IDs remain stable and unchanged.
- Invalid subject/capability inputs continue returning explicit errors.

## Validation

Required checks:

- `./build/scripts/test.sh capability_table`
- `./build/scripts/test.sh capability_gate`
- `./build/scripts/validate_bundle.sh`

Expected evidence includes deterministic markers for debug-exit deny/allow/revoke paths and a green validator report.

## Deliverables

- Capability contract updated with `CAP_DEBUG_EXIT`.
- Gate API implementation + tests.
- Updated docs:
  - `docs/architecture/CAPABILITIES.md`
  - `docs/planning/M0-M1-task-registry.md`

## Done definition

CAP-008 is complete when one small PR merges with green CI, marker-verified proof that debug-exit is explicitly capability-gated, and docs/registry reflect the new completed task.
