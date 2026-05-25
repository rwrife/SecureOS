# SecureOS Build Roadmap (Tooling-First to Capability-Native OS)

This roadmap turns the product vision into an execution sequence with concrete deliverables, validation gates, and agent-friendly workflows.

## 1) Execution Principles

- **Tooling first, features second**: no subsystem work lands unless it runs through repeatable containerized build + QEMU validation.
- **Vertical slices over horizontal layers**: each milestone must boot and demonstrate one user-visible capability boundary.
- **Capability correctness over convenience**: every interface must be explicit, deny-by-default, and testable via negative-path tests.
- **Agent-compatible by design**: every task has machine-verifiable acceptance criteria and deterministic test commands.

## 2) Repository and Build Bootstrap (Phase 0)

## 2.1 Target baseline

- Host-independent, deterministic builds in containerized toolchain.
- x86 target in QEMU first (BIOS + Multiboot2 or Limine-compatible handoff).
- Serial log is source of truth for CI and agent validation.

## 2.2 Proposed repo structure

```text
SecureOS/
  docs/
    architecture/
    abi/
    test-plans/
  build/
    docker/
    scripts/
    qemu/
  kernel/
    arch/x86/
    core/
    mm/
    sched/
    ipc/
    cap/
  modules/
    console/
    launcher/
    broker/
    faux/
  sdk/
    include/os/
    lib/
    tools/
  tests/
    boot/
    integration/
    matrix/
  manifests/
  artifacts/
```

## 2.3 Containerized toolchain tasks

1. Create `build/docker/Dockerfile.toolchain` with:
   - `clang`/`lld` (or GCC/binutils), `nasm`, `grub`/`xorriso` (if ISO flow), `qemu-system-x86_64`, `python3`, `make`/`ninja`.
2. Pin versions and publish image digest in `build/toolchain.lock`.
3. Add entrypoint wrappers:
   - `build/scripts/build.sh`
   - `build/scripts/test.sh`
   - `build/scripts/run_qemu.sh`
4. Ensure all developer and agent commands run through the same container image.

## 2.4 Build system tasks

- Select build orchestrator (CMake + Ninja is practical for mixed C/ASM; Rust option acceptable if chosen globally).
- Create top-level targets:
  - `kernel`
  - `modules`
  - `image`
  - `run`
  - `test-boot`
- Emit map files and symbols for debugging (`.map`, optional DWARF artifact).

## 2.5 QEMU harness setup tasks

- Standardize headless invocation:
  - `-nographic -serial stdio`
  - deterministic memory/CPU flags
  - debug-exit device (e.g., `isa-debug-exit`) for pass/fail signaling
- Capture logs to `artifacts/qemu/<testname>.log`.
- Enforce wall-clock timeout in harness script.
- Parse logs for structured test markers:
  - `TEST:START:<name>`
  - `TEST:PASS:<name>`
  - `TEST:FAIL:<name>:<reason>`

## 2.6 Acceptance gate for Phase 0

- One command builds boot image from clean checkout in container.
- One command boots in QEMU and returns pass/fail exit status.
- CI reproduces same result with no local host dependencies.

## 3) First Technical Goal: x86 Boot Path + Hello World Validation Slice

This is the first implementation slice and must be completed before broader kernel/service development.

## 3.1 Boot flow objective

Implement minimal boot pipeline:

1. Bootloader loads kernel image.
2. Early startup code initializes CPU mode (32-bit protected mode and/or long mode depending on entry strategy).
3. Establish basic stack and descriptor tables as required.
4. Transfer control to `kmain`.
5. Print `Hello, SecureOS` to VGA text mode and serial.
6. Exit via debug-exit code for automated pass.

## 3.2 Bootloader strategy options

Choose one path and commit early to avoid churn:

- **Option A (fastest path)**: Limine/Multiboot2-compliant boot protocol.
  - Pros: less handwritten boot code, easier early momentum.
  - Cons: slightly less ownership of very earliest stage.
- **Option B (educational/full control)**: custom stage-1/stage-2 bootloader + mode switching.
  - Pros: total control of real-mode to protected/long-mode transitions.
  - Cons: slower and more error-prone early path.

Recommended for this project: **Option A first**, with optional later custom loader milestone.

## 3.3 Concrete implementation backlog

1. Add `kernel/arch/x86/boot/` with startup ASM and linker script.
2. Configure memory layout and kernel virtual/physical base assumptions.
3. Implement serial driver (COM1) for deterministic logs.
4. Implement minimal VGA text writer for visible local output.
5. Add tiny test framework macros for boot tests.
6. Add debug-exit helper to signal success/failure to QEMU.

## 3.4 Hello world validation tests

- **Boot smoke**: kernel banner appears on serial.
- **Rendering smoke**: `Hello, SecureOS` emitted to serial and VGA path.
- **Exit semantics**: debug-exit code maps to harness pass.
- **Negative test**: intentionally failing test returns fail code and is caught by harness.

## 3.5 Done definition for first goal

- `build/scripts/run_qemu.sh --test hello_boot` returns success.
- Log contains expected markers and hello string.
- CI + local container run produce identical pass.

## 4) Agentic Build/Test System (Parallel Foundation)

## 4.1 Task model

Track work as a DAG with:

- task id, owner role, dependencies, command(s), expected outputs, validator rules.

Example fields:

- `task_id`: `BOOT-HELLO-001`
- `depends_on`: `TOOLCHAIN-001`
- `run`: `build/scripts/test.sh boot_hello`
- `pass_condition`: `exit_code == 0 && log_has('TEST:PASS:boot_hello')`

## 4.2 Agent role contracts

- **Planner**: expands milestones into leaf DAG tasks.
- **Implementer**: changes code/docs/scripts only.
- **Test Engineer**: adds/updates automated tests and harness logic.
- **Validator**: read-only on code, can only run checks and update task statuses.

## 4.3 Deterministic tool API wrappers

Create wrapper commands that agents must use:

- `os-build`
- `os-package`
- `os-run-qemu`
- `os-validate`
- `os-snapshot`

Each wrapper must:

- emit machine-readable JSON summary,
- include artifact paths,
- include stable pass/fail fields.

Wrapper contract and JSON envelope: see
[`docs/test-plans/wrappers.md`](docs/test-plans/wrappers.md). Implementation
lives under `build/scripts/os-*` (sh + ps1 peers); each wrapper mirrors its
envelope into `artifacts/runs/<run_id>/<tool>.json` per §4.4.

## 4.4 Artifact policy

Store per-run outputs under `artifacts/runs/<timestamp-or-id>/`:

- build metadata
- image hashes
- qemu commandline
- serial logs
- validator report JSON

This enables replay, diffing, and agent postmortems.

See [`docs/test-plans/artifact-bundle.md`](docs/test-plans/artifact-bundle.md) for the
on-disk layout, `<id>` derivation rule, and the `SECUREOS_RUN_ID` env-var
contract that keeps all artifacts from one run co-located.

## 5) Milestones After Boot Slice

## 5.1 M1: Minimal kernel isolation + IPC skeleton

Deliver:

- process abstraction/address-space boundary
- synchronous IPC primitive
- kernel capability table skeleton

Validate:

- two modules exchange message — satisfied by `TEST:PASS:m1_ipc_allow`
  (see `tests/m1_ipc_demo_test.c`, issue #251, plan #198 slice 4).
- unauthorized operation denied with explicit error — satisfied by
  `TEST:PASS:m1_ipc_deny` + canonical `CAP:DENY:<m1-unauth>:ipc_send:-`
  marker (same harness).

## 5.2 M2: Console service + Launcher + HelloApp (first capability slice)

Deliver:

- console service module
- launcher module with manifest-based cap grants
- HelloApp requesting console-write capability

Validate:

- grant path: HelloApp prints — satisfied by
  `TEST:PASS:helloapp_allowed_console_write` + `TEST:PASS:helloapp_allow`
  (see `tests/helloapp_allow_test.c`, run via
  `build/scripts/test.sh helloapp_allow`; plan
  `plans/2026-04-14-console-service-launcher-helloapp.md`, umbrella issue
  #82).
- deny path: HelloApp cannot print and receives defined error/fallback —
  satisfied by `TEST:PASS:helloapp_denied_console_write` +
  `TEST:PASS:helloapp_deny` (see `tests/helloapp_deny_test.c`, run via
  `build/scripts/test.sh helloapp_deny`; same plan / issue #82).
- launcher-mediation regression: direct console writes without an explicit
  launcher-issued grant remain denied, and revoke restores the deny path —
  satisfied by `TEST:PASS:launcher_console_deny_without_grant`,
  `TEST:PASS:launcher_console_allow_after_grant`,
  `TEST:PASS:launcher_console_regression_bypass_denied`, and
  `TEST:PASS:launcher_console_revoke_restores_deny` (see
  `tests/launcher_console_test.c`, run via
  `build/scripts/test.sh launcher_console`).

## 5.3 M3: Filesystem service + faux FS

Deliver:

- ramfs-backed persistent namespace (initially in-memory)
- faux ephemeral FS provider using same interface
- optional capability substitution rules

Validate:

- allow path: app with granted persistent FS cap can create, write, and
  read back files, with state surviving relaunch — satisfied by
  `TEST:PASS:fs_service_persist_allow` plus sub-checks
  `:cap_present`, `:write_succeeds`, `:read_back_after_close`, and
  `:relaunch_round_trip` (see `tests/fs_service_persist_allow_test.c`,
  run via `build/scripts/test.sh fs_service_persist_allow`).
- deny path: editor with denied persistent FS fails closed and is
  transparently redirected to ephemeral scope (no persistent visibility) —
  satisfied by `TEST:PASS:fs_service_persist_deny` plus sub-checks
  `:cap_absent`, `:fail_closed`, `:redirected_to_ephemeral`,
  `:no_persist_visibility:fail_closed`, and
  `:no_persist_visibility:redirected`. The canonical capability-audit
  `CAP:DENY` marker is currently gated behind the audit ring wiring
  (#84 / #98) and the slice asserts the explicit
  `TEST:SKIP:fs_service_persist_deny:audit_deny_recorded:audit_log_unwired`
  marker so the validator distinguishes "not asserted" from
  "asserted and passed" (see `tests/fs_service_persist_deny_test.c`
  and `build/scripts/test_fs_service_persist_deny.sh`, run via
  `build/scripts/test.sh fs_service_persist_deny`).
- ephemeral-scope writes succeed in-session — satisfied by
  `TEST:PASS:fs_service_ephemeral_reset:write_to_faux_succeeds` and
  `:visible_in_same_session` (see
  `tests/fs_service_ephemeral_reset_test.c`, run via
  `build/scripts/test.sh fs_service_ephemeral_reset`).
- data disappears after app exit/relaunch — satisfied by
  `TEST:PASS:fs_service_ephemeral_reset:gone_after_relaunch` and
  `:no_persist_leak`, with the umbrella
  `TEST:PASS:fs_service_ephemeral_reset` marker (same test file / runner).

These markers are anchored by plan
`plans/2026-05-24-m3-fs-on-m1-substrate.md` (umbrella issue #277) and
complemented by the open M3-on-M1 substrate slices #279
(`launcher_fs_spawn_app_with_fs_caps` + ipc_scratch fs-handle handoff),
#280 (HelloApp fs-demo variant + persist allow/deny `_qemu` peers), and
#281 (ephemeral-reset `_qemu` peer covering all four §5.3 reset sub-checks),
which extend the same contract end-to-end under QEMU — the same pattern
§5.2 uses for plan #259 + its closed slice issues.

## 5.4 M4: Capability Broker + share workflow

Deliver:

- broker service and prompt policy integration
- provider/consumer contracts (`DocumentProvider`, `AttachmentConsumer`)
- scoped doc-read capability issuance

Validate:

- allow flow succeeds and logs audited transfer — satisfied by
  `TEST:PASS:broker_share_allow` plus sub-checks
  `:owner_holds_cap`, `:request_returns_pending_share_id`,
  `:approve_grants_recipient`, `:scope_is_resource_bound`, and
  `:scope_is_capability_bound` (see `tests/broker_share_allow_test.c`,
  run via `build/scripts/test.sh broker_share_allow`). The `_qemu`
  substrate peer asserts the same contract end-to-end with the
  `_qemu`-suffixed marker spellings
  `TEST:PASS:m4_broker_share_allow_qemu` plus sub-checks
  `:owner_holds_cap_qemu`, `:request_returns_pending_share_id_qemu`,
  `:approve_grants_recipient_qemu`, `:scope_is_resource_bound_qemu`, and
  `:scope_is_capability_bound_qemu` (see
  `tests/m4_broker_share_allow_qemu_test.c`, run via
  `build/scripts/test.sh m4_broker_share_allow_qemu` /
  `build/scripts/test_m4_broker_share_allow_qemu.sh`). The audit-side
  `audit_grant_recorded` assertion is currently gated behind the
  broker→audit wiring follow-up #311 (depends on #84 / #98) and the
  host slice asserts the explicit
  `TEST:SKIP:broker_share_allow:audit_grant_recorded:broker_audit_unwired_pending_issue_98`
  marker so the validator distinguishes "not asserted" from
  "asserted and passed".
- deny flow issues no cap and app handles gracefully — satisfied by
  `TEST:PASS:broker_share_deny` plus sub-checks
  `:owner_holds_cap`, `:request_returns_pending_share_id`, `:deny_path`,
  `:no_recipient_grant`, `:cannot_be_re_approved`, and
  `:bystander_cannot_mutate` (see `tests/broker_share_deny_test.c`,
  run via `build/scripts/test.sh broker_share_deny`). The `_qemu`
  substrate peer asserts the same contract under QEMU with markers
  `TEST:PASS:m4_broker_share_deny_qemu` plus sub-checks
  `:request_returns_pending_share_id_qemu`, `:deny_blocks_recipient_qemu`,
  `:cannot_be_re_approved_qemu`, `:bystander_cannot_mutate_qemu`,
  `:scope_is_resource_bound_qemu`, and `:scope_is_capability_bound_qemu`
  (see `tests/m4_broker_share_deny_qemu_test.c`, run via
  `build/scripts/test.sh m4_broker_share_deny_qemu` /
  `build/scripts/test_m4_broker_share_deny_qemu.sh`). Both host and
  `_qemu` slices currently emit
  `TEST:SKIP:broker_share_deny:audit_deny_recorded:broker_audit_unwired_pending_issue_98`
  and
  `TEST:SKIP:m4_broker_share_deny_qemu:audit_deny_recorded_qemu:broker_audit_unwired_pending_issue_98`
  respectively, gated on the broker→audit wiring follow-up #311.
- revoke flow restores the deny posture and is idempotent — satisfied
  by `TEST:PASS:broker_share_revoke` plus sub-checks
  `:setup_grants_recipient`, `:owner_revoke_takes_effect`,
  `:underlying_table_revoked`, `:double_revoke_is_idempotent`, and
  `:recipient_self_revoke` (see `tests/broker_share_revoke_test.c`,
  run via `build/scripts/test.sh broker_share_revoke`). The `_qemu`
  substrate peer asserts the same contract under QEMU with markers
  `TEST:PASS:m4_broker_share_revoke_qemu` plus sub-checks
  `:setup_grants_recipient_qemu`, `:owner_revoke_takes_effect_qemu`,
  `:underlying_table_revoked_qemu`, `:double_revoke_is_idempotent_qemu`,
  `:order_observed_qemu`, `:recipient_self_revoke_qemu`, and
  `:process_destroy_recycle_revokes` (see
  `tests/m4_broker_share_revoke_qemu_test.c`, run via
  `build/scripts/test.sh m4_broker_share_revoke_qemu` /
  `build/scripts/test_m4_broker_share_revoke_qemu.sh`). Both host and
  `_qemu` slices emit
  `TEST:SKIP:broker_share_revoke:audit_revoke_recorded:broker_audit_unwired_pending_issue_98`
  and
  `TEST:SKIP:m4_broker_share_revoke_qemu:audit_revoke_recorded_qemu:broker_audit_unwired_pending_issue_98`
  respectively, gated on the broker→audit wiring follow-up #311.

These markers are anchored by the M4 host-fixture acceptance work in
#115 (broker_share_{allow,deny,revoke} + 16 sub-checks) and the
substrate plan #299 (M4 capability broker re-platformed onto merged M1
substrate + M2/M3 service-module precedent), with the `_qemu` peers
landed by #304 (allow + deny) and #305 (revoke), matching how §5.2
references plan #259 and §5.3 references plan #277 + its closed slice
issues. The currently-SKIPped `audit_*_recorded` markers will be
turned into PASS by the broker→audit wiring follow-up #311 (which
depends on the audit-ring wiring tracked in #84 / #98).

## 5.5 M5: Ownership graph + cascading deletion

Deliver:

- ownership metadata and revocation logic
- subtree deletion semantics

Validate:

- deleting Launcher removes owned app modules/resources
- delegated caps derived from deleted owner become invalid

## 5.6 M6: Public SDK + external app template

Deliver:

- headers + userland library
- tool wrappers (`os-cc`, `os-pack`, `os-run`)
- manifest schema and ABI versioning guide

Validate:

- third-party sample app builds and runs in QEMU
- manifest capability declarations enforced by launcher/broker

## 6) CI/CD and Test Matrix Strategy

## 6.1 CI stages

1. lint/format/static checks
2. build image in pinned container
3. boot smoke tests
4. integration tests (capability + broker flows)
5. nightly matrix (grant/deny permutations)

## 6.2 Matrix dimensions

- capability sets (minimal, typical, full)
- substitution policy toggles (faux on/off where legal)
- lifecycle events (owner deleted during active delegation)

## 6.3 Quality gates

- no merge on failing boot or capability regression tests
- require deterministic artifact hashes for release candidates
  (see `docs/build/determinism.md`; CI step
  `image-determinism` builds twice and asserts
  `sha256(secureos-disk.img)` is stable, currently non-blocking baseline
  per issue #174)
- require validator-generated pass evidence for milestone closure

## 7) ABI and Interface Freeze Plan

Define and version early to prevent churn:

- syscall ABI (if exposed)
- IPC wire format and error model
- capability handle representation and revocation behavior
- module manifest schema and compatibility policy

Suggested policy:

- start at `OS_ABI_VERSION=0` during rapid iteration,
- freeze to `1` once SDK beta is announced,
- maintain compatibility shims for at least one major version.

In-tree source of truth: `user/include/secureos_abi.h` defines
`OS_ABI_VERSION_MAJOR`, `OS_ABI_VERSION_MINOR`, and the packed
`OS_ABI_VERSION` constant; `os_get_abi_version()` exposes it at runtime.

The canonical ABI documentation lives under [`docs/abi/`](docs/abi/README.md);
each of the four §7 surfaces has a dedicated spec there:

- syscall ABI — [`docs/abi/syscalls.md`](docs/abi/syscalls.md)
- IPC wire format and error model — [`docs/abi/ipc-wire.md`](docs/abi/ipc-wire.md)
- capability handle representation + revocation —
  [`docs/abi/capability-handle.md`](docs/abi/capability-handle.md)
  (see also [`docs/abi/capabilities.md`](docs/abi/capabilities.md))
- module / launcher manifest schema + compatibility —
  [`docs/abi/manifest.md`](docs/abi/manifest.md)
- `OS_ABI_VERSION` policy and field layout —
  [`docs/abi/versioning.md`](docs/abi/versioning.md)

All new ABI-shaped surface (new syscall, new IPC opcode, new manifest
field, new capability handle bit) must land its spec under `docs/abi/`
in the same change that introduces it.

## 8) Immediate Next 14 Tasks (actionable backlog)

1. Add pinned toolchain Dockerfile and lock file.
2. Add canonical build/test/run scripts.
3. Add minimal CI workflow invoking containerized scripts.
4. Add x86 linker script and kernel entry assembly.
5. Add serial writer and structured test logger.
6. Add VGA text writer for hello output.
7. Add QEMU debug-exit signaling helper.
8. Add `hello_boot` test target and harness parser.
9. Add failing-test fixture to verify negative-path detection.
10. Add machine-readable validator report format.
11. Add task DAG schema (`docs/test-plans/task-schema.md`). — see [`docs/test-plans/task-schema.md`](docs/test-plans/task-schema.md) for the canonical field list, `pass_condition` DSL, and worked example.
12. Add initial milestone/task registry (`docs/test-plans/m0-m1-plan.yaml`).
13. Add coding conventions for kernel/module boundaries.
14. Add architecture decision record selecting boot protocol.

## 9) Risks and Mitigations

- **Risk: early boot instability slows progress**
  - Mitigation: keep first slice tiny, use proven boot protocol, rely on serial-first debugging.
- **Risk: agent drift / inconsistent task completion**
  - Mitigation: validator-only completion with command-output-backed criteria.
- **Risk: ABI churn breaks modules**
  - Mitigation: publish ABI docs early and version every interface change.
- **Risk: non-deterministic tests**
  - Mitigation: fixed QEMU flags, fixed seeds/timeouts, artifact capture and replay.

## 10) Success Checklist for the initial objective

- [ ] Containerized toolchain builds from clean clone.
- [ ] QEMU harness boots image headlessly and captures serial logs.
- [ ] Kernel reaches 32/64-bit intended mode and enters `kmain` reliably.
- [ ] `Hello, SecureOS` visible through serial (and VGA path implemented).
- [ ] Harness returns deterministic pass/fail through debug-exit.
- [ ] Artifacts and validator report are stored for both humans and agents.

When this checklist passes, proceed directly to the next vertical slice:

**Console service + Launcher + HelloApp with explicit console-write capability and deny-path test.**
