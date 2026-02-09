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

## 4.4 Artifact policy

Store per-run outputs under `artifacts/runs/<timestamp-or-id>/`:

- build metadata
- image hashes
- qemu commandline
- serial logs
- validator report JSON

This enables replay, diffing, and agent postmortems.

## 5) Milestones After Boot Slice

## 5.1 M1: Minimal kernel isolation + IPC skeleton

Deliver:

- process abstraction/address-space boundary
- synchronous IPC primitive
- kernel capability table skeleton

Validate:

- two modules exchange message
- unauthorized operation denied with explicit error

## 5.2 M2: Console service + Launcher + HelloApp (first capability slice)

Deliver:

- console service module
- launcher module with manifest-based cap grants
- HelloApp requesting console-write capability

Validate:

- grant path: HelloApp prints
- deny path: HelloApp cannot print and receives defined error/fallback

## 5.3 M3: Filesystem service + faux FS

Deliver:

- ramfs-backed persistent namespace (initially in-memory)
- faux ephemeral FS provider using same interface
- optional capability substitution rules

Validate:

- editor with denied persistent FS still saves in ephemeral scope
- data disappears after app exit/relaunch

## 5.4 M4: Capability Broker + share workflow

Deliver:

- broker service and prompt policy integration
- provider/consumer contracts (`DocumentProvider`, `AttachmentConsumer`)
- scoped doc-read capability issuance

Validate:

- allow flow succeeds and logs audited transfer
- deny flow issues no cap and app handles gracefully

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
11. Add task DAG schema (`docs/test-plans/task-schema.md`).
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
