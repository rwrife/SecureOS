# SecureOS

SecureOS is an experimental operating system project focused on **zero-trust by default** behavior.

## Objective

Build a small, capability-native OS where resource access is explicit, deny-by-default, and testable.

Core principles:
- tooling-first, deterministic builds
- vertical slices over broad unfinished layers
- strict validation of allow/deny paths

## Current State

What exists today:
- macOS bootstrap script for local developer setup (`scripts/setup-macos.sh`)
- pinned containerized toolchain baseline (`build/docker/Dockerfile.toolchain`, `build/toolchain.lock`)
- canonical build/test/run wrappers under `build/scripts/`
- deterministic headless QEMU harness with log + metadata outputs
- minimal boot-sector smoke path that prints a serial success marker

In progress:
- moving from boot-sector smoke validation toward full kernel boot path to `kmain`
- roadmap-driven issue queue for Phase 0 / boot milestone execution

## Quick Start (Local)

### 1) Optional: bootstrap macOS deps

```bash
./scripts/setup-macos.sh
```

### 2) Run the current validation paths

```bash
./build/scripts/test.sh hello_boot
./build/scripts/test.sh kernel_console
./build/scripts/test.sh kernel_filedemo
./build/scripts/test.sh kernel_persistence
```

Windows PowerShell:

```powershell
.\build\scripts\test.ps1 hello_boot
.\build\scripts\test.ps1 kernel_console
.\build\scripts\test.ps1 kernel_filedemo
.\build\scripts\test.ps1 kernel_persistence
```

Expected success markers include:
- `SecureOS boot sector OK`
- `QEMU_PASS:hello_boot`
- `QEMU_PASS:kernel_console`
- `QEMU_PASS:kernel_filedemo`
- `QEMU_PASS:kernel_persistence`

### 3) Check artifacts

```bash
ls -la artifacts/qemu/
./build/scripts/validate_bundle.sh
ls -la artifacts/runs/
```

Windows PowerShell:

```powershell
Get-ChildItem artifacts\qemu
.\build\scripts\validate_bundle.ps1
Get-ChildItem artifacts\runs
```

You should see:
- `artifacts/qemu/hello_boot.log`
- `artifacts/qemu/hello_boot.meta.json`
- `artifacts/qemu/kernel_console.log`
- `artifacts/qemu/kernel_filedemo.log`
- `artifacts/qemu/kernel_persistence.log`
- `artifacts/kernel/secureos.iso`
- `artifacts/disk/secureos-disk.img`
- `artifacts/runs/<run-id>/build_metadata.json`
- `artifacts/runs/<run-id>/validator_report.json`

### 4) Build the sample user app

```bash
./build/scripts/build.sh user-app
```

Windows PowerShell:

```powershell
.\build\scripts\build.ps1 user-app
```

This produces:
- `artifacts/user/filedemo.o`
- `artifacts/user/filedemo.elf`

Inside the kernel console, the current demo flow is:
- `help`
- `storage`
- `apps`
- `run filedemo`
- `cat appdemo.txt`
- `exit pass`

### 5) Boot To Interactive Command Prompt

Use the dedicated interactive boot wrappers to get a live `secureos>` prompt:

```bash
./build/scripts/build.sh console
```

Windows PowerShell:

```powershell
.\build\scripts\build.ps1 console
```

This boots the kernel ISO with the attached disk image and leaves QEMU interactive.
Type commands directly at `secureos>`. Use `exit pass` to stop QEMU cleanly.

If a previous SecureOS QEMU/container session is still running, the boot and disk-image scripts
now stop those stale instances automatically so `secureos-disk.img` can be reused.

## Repo Pointers

- Roadmap: `BUILD_ROADMAP.md`
- M0→M1 task registry: `docs/planning/M0-M1-task-registry.md`
- Coding conventions: `docs/CODING_CONVENTIONS.md`
- ADRs: `docs/adr/`
- Toolchain container: `build/docker/`
- Build wrappers: `build/scripts/`
- QEMU args: `build/qemu/`
- Boot experiment: `experiments/bootloader/`
