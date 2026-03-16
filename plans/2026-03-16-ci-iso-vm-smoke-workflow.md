# 2026-03-16 CI ISO VM Smoke Workflow

## Goal

Add a dedicated GitHub Actions workflow that builds `artifacts/kernel/secureos.iso` and validates that it boots in headless QEMU before publishing artifacts.

## Scope

- Create `.github/workflows/iso-vm-build.yml`.
- Reuse existing wrapper scripts instead of duplicating build logic.
- Publish kernel, disk, and QEMU artifacts to support investigation of CI failures.

## Workflow Design

1. Checkout repository source.
2. Build pinned toolchain image from `build/docker/Dockerfile.toolchain`.
3. Install host runtime dependencies for VM smoke boot (`qemu-system-x86`, `python3`).
4. Build ISO via `./build/scripts/build.sh image`.
5. Build disk via `./build/scripts/build.sh disk`.
6. Run VM smoke boot via `./build/scripts/run_qemu.sh --test kernel_console`.
7. Upload `artifacts/kernel/`, `artifacts/disk/`, and `artifacts/qemu/`.

## Success Criteria

- Workflow can be manually started with `workflow_dispatch`.
- Workflow runs automatically for pull requests and pushes to `main`.
- `kernel_console` smoke test must pass for workflow success.
- ISO is available as a downloadable CI artifact.

## Risks And Mitigations

- Risk: Toolchain/runtime dependency drift in GitHub runners.
- Mitigation: Build the pinned toolchain container during workflow execution.

- Risk: QEMU smoke test flakes due to timing.
- Mitigation: Reuse existing deterministic QEMU args and test harness already used by repository scripts.
