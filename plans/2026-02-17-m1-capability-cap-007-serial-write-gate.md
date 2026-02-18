# SecureOS Plan — M1 CAP-007 Serial Write Capability + Explicit Gate

_Date: 2026-02-17_

## Why this is next

CAP-006 established scalable bitset-backed capability storage while preserving deny-by-default behavior. The next incremental step is to prove the table supports more than one capability ID by introducing a second privileged operation boundary.

Adding an explicit serial-write capability extends zero-trust enforcement without changing scheduler/process architecture yet.

## Goal

Implement CAP-007 by adding `CAP_SERIAL_WRITE` and enforcing it through a dedicated gate, with deterministic tests for allow/deny/invalid paths.

## Scope

### In scope

1. Extend capability ID set with `CAP_SERIAL_WRITE` as append-only ID `2`.
2. Add `cap_serial_write_gate(...)` to enforce capability check semantics for serial output operations.
3. Keep `cap_console_write_gate(...)` behavior unchanged.
4. Expand tests to verify:
   - default deny for both capability IDs;
   - subject-local grants for each capability;
   - no cross-capability leakage;
   - explicit invalid subject/capability errors remain stable.
5. Update architecture and planning docs to mark CAP-006 done and CAP-007 in-progress.

### Out of scope

- Wiring serial gate into full runtime I/O subsystem
- Dynamic policy/manifest loader
- Userspace capability broker
- ABI rework beyond additive capability ID/gate API

## Zero-trust invariants

- All capabilities remain deny-by-default after reset/init.
- A grant for `CAP_CONSOLE_WRITE` must not imply serial-write access.
- A grant for `CAP_SERIAL_WRITE` must not imply console-write access.
- Invalid subject and capability IDs must return explicit errors.

## Implementation tasks (single small PR)

1. Add `CAP_SERIAL_WRITE = 2` to capability contract.
2. Add `cap_serial_write_gate(...)` in `kernel/cap/cap_gate.{h,c}` mirroring existing explicit check semantics.
3. Extend `tests/capability_table_test.c` for dual-capability isolation checks.
4. Extend `tests/capability_gate_test.c` for serial gate allow/deny/revoke behavior.
5. Update `docs/architecture/CAPABILITIES.md` + `docs/planning/M0-M1-task-registry.md` status rows.

## Acceptance criteria

- `./build/scripts/test.sh cap_api_contract` passes.
- `./build/scripts/test.sh capability_table` passes.
- `./build/scripts/test.sh capability_gate` passes.
- `./build/scripts/validate_bundle.sh` passes.
- CI passes on implementation PR.

## Risks and mitigations

- **Risk:** Capability ID expansion breaks bitset boundaries.
  - **Mitigation:** Add cross-capability isolation checks in table tests.
- **Risk:** New gate introduces inconsistent return semantics.
  - **Mitigation:** Reuse explicit `cap_check` result path and assert invalid-input behavior in tests.

## Done definition

CAP-007 is complete when serial-write capability and gate are merged with deterministic tests proving deny-by-default + no cross-capability leakage, and docs/registry reflect completion.