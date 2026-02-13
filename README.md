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

### 2) Run the current smoke validation

```bash
./build/scripts/test.sh hello_boot
```

Expected success markers include:
- `SecureOS boot sector OK`
- `QEMU_PASS:hello_boot`

### 3) Check artifacts

```bash
ls -la artifacts/qemu/
```

You should see:
- `artifacts/qemu/hello_boot.log`
- `artifacts/qemu/hello_boot.meta.json`

## Repo Pointers

- Roadmap: `BUILD_ROADMAP.md`
- Toolchain container: `build/docker/`
- Build wrappers: `build/scripts/`
- QEMU args: `build/qemu/`
- Boot experiment: `experiments/bootloader/`
