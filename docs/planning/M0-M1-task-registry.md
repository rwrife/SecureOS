# M0 → M1 Task Registry

This registry tracks the concrete, ordered tasks to move SecureOS from the current Phase-0 boot/tooling baseline (M0) to the first enforceable capability boundary (M1).

## Status keys

- **todo** — defined but not started
- **in-progress** — active implementation
- **blocked** — cannot proceed until dependency resolves
- **done** — merged to `main` and validated

## Dependency graph overview

`M0-TOOLING-*` → `M0-BOOT-*` → `M1-CAP-*`

## Registry

| Task ID | Milestone | Title | Depends On | Validation Command | Status |
|---|---|---|---|---|---|
| M0-TOOLING-001 | M0 | Pin toolchain image and lock metadata | - | `./build/scripts/build.sh` | done |
| M0-TOOLING-002 | M0 | Canonical build/test/run wrapper scripts | M0-TOOLING-001 | `./build/scripts/test.sh hello_boot` | done |
| M0-BOOT-001 | M0 | Deterministic QEMU harness args + metadata | M0-TOOLING-002 | `./build/scripts/test.sh hello_boot` | done |
| M0-BOOT-002 | M0 | Boot smoke marker parser and pass/fail contract | M0-BOOT-001 | `./build/scripts/test.sh hello_boot` | done |
| M0-BOOT-003 | M0 | Negative-path failing fixture in harness | M0-BOOT-002 | `./build/scripts/test.sh hello_boot_negative` | done |
| M1-CAP-001 | M1 | Capability IDs + check API skeleton | M0-BOOT-003 | `./build/scripts/test.sh cap_api_contract` | done |
| M1-CAP-002 | M1 | Per-subject capability table (deny-by-default) | M1-CAP-001 | `./build/scripts/test.sh capability_table` | done |
| M1-CAP-003 | M1 | Gate first privileged operation behind capability check | M1-CAP-002 | `./build/scripts/test.sh capability_gate` | done |
| M1-CAP-004 | M1 | Capability allow/deny markers integrated in harness | M1-CAP-003 | `./build/scripts/test.sh capability_gate` | done |
| M1-CAP-005 | M1 | Capability core ADR + architecture docs | M1-CAP-004 | `./build/scripts/test.sh hello_boot` | done |
| M1-CAP-006 | M1 | Bitset-backed subject capability storage migration | M1-CAP-005 | `./build/scripts/test.sh capability_table` | done |
| M1-CAP-007 | M1 | Serial-write capability boundary + dual-cap isolation tests | M1-CAP-006 | `./build/scripts/test.sh capability_gate` | done |

## Notes

- All M1 tasks must preserve zero-trust defaults (no implicit capability grants).
- Each task should be landed as a focused PR with deterministic validation output.
- If a task cannot define a machine-verifiable pass condition, it is not ready for implementation.
