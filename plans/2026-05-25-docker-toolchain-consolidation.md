# Docker Toolchain Consolidation & Demo Prep

**Date:** 2026-05-25  
**Status:** Draft  
**Goal:** Clone the repo, run one `start` script, and have SecureOS booted in QEMU — on any platform.

---

## Problem Statement

The repo currently supports multiple build approaches:

1. **Native toolchain** — `scripts/setup-macos.sh` installs Homebrew packages (clang, nasm, qemu, etc.) for bare-metal builds.
2. **Docker toolchain** — `build/docker/Dockerfile.toolchain` provides a pinned container image.
3. **Hybrid detection** — Every `.sh` script checks `command -v docker` and either delegates into the container or runs natively. This creates two untested code paths per script.
4. **Dual .sh/.ps1 parity** — 100+ scripts exist in paired `.sh`/`.ps1` form (many only in `.sh`, tracked in `.shell_parity_allowlist`). The `.ps1` wrappers for `os-*` commands already just invoke the `.sh` peer inside Docker.

This leads to:
- Massive maintenance surface (~120 shell scripts + ~40 PowerShell scripts)
- A 90-entry parity allowlist of technical debt
- Confusion about "which way do I build?" for newcomers
- Risk of native-vs-container divergence bugs

## Proposed Architecture

### Docker as the build toolchain, per-platform host scripts

```
Host (any OS)
  ├── scripts/setup-{windows,macos,linux}.{ps1,sh}  ← installs Docker + host QEMU
  ├── scripts/build.{ps1,sh}                        ← invokes Docker to compile
  ├── scripts/boot.{ps1,sh}                         ← launches QEMU on the host
  └── Docker container (secureos/toolchain:bookworm)
        └── all compilation, linking, ISO creation (bash only, no platform branching)
```

**Key insight:** Docker handles the *build toolchain* (compiler, linker, grub, xorriso). QEMU runs on the *host* for display/device access. The `start` script orchestrates everything: checks deps → installs if needed → builds → boots.

---

## User Experience (the "clone and run" flow)

```bash
# macOS / Linux
git clone https://github.com/rwrife/SecureOS.git
cd SecureOS
./start.sh

# Windows (PowerShell)
git clone https://github.com/rwrife/SecureOS.git
cd SecureOS
.\start.ps1
```

The `start` script:
1. Checks for Docker and QEMU — if missing, runs the platform setup automatically (with user confirmation)
2. Ensures the toolchain Docker image is built
3. Compiles the kernel and builds the disk image inside Docker
4. Launches QEMU on the host with the built image

Optional flags:
```
./start.sh --setup-only    # Just install dependencies, don't build/boot
./start.sh --build-only    # Build but don't boot
./start.sh --graphics      # Boot with VGA/framebuffer GUI instead of serial console
./start.sh --skip-setup    # Skip dependency checks (CI use case)
./start.sh --clean         # Remove artifacts before building
```

Default mode is headless serial console (text output piped to the terminal). The `--graphics` flag launches QEMU with a display window showing the OS graphical interface.

---

## Script Layout

### Root — user-facing entry points

| Script | Purpose |
|--------|---------|
| `start.sh` | **The one script** — setup + build + boot (macOS/Linux) |
| `start.ps1` | **The one script** — setup + build + boot (Windows) |

### `scripts/` — individual steps (called by `start`, also usable standalone)

| Script | Purpose |
|--------|---------|
| `scripts/setup-windows.ps1` | Install Docker Desktop + QEMU via winget/choco; verify |
| `scripts/setup-macos.sh` | Install Docker (Colima or Desktop) + QEMU via Homebrew; verify |
| `scripts/setup-linux.sh` | Install Docker Engine + QEMU via apt/dnf; verify |
| `scripts/build.ps1` | Windows: build via `docker run` into toolchain container |
| `scripts/build.sh` | macOS/Linux: build via `docker run` into toolchain container |
| `scripts/boot.ps1` | Windows: launch QEMU with built ISO/disk image |
| `scripts/boot.sh` | macOS/Linux: launch QEMU with built ISO/disk image |
| `scripts/test.sh` | macOS/Linux: run test suite in container |
| `scripts/test.ps1` | Windows: run test suite in container |

Each sub-script is independently runnable for iterative development:
```
scripts/build.sh [kernel|disk|all]    # default: all
scripts/boot.sh [console|graphics]    # default: console
```

### `build/scripts/` — container-internal (bash only)

These run *inside* the Docker container. No Docker-detection logic. No `.ps1` peers.

| Script | Purpose |
|--------|---------|
| `build/scripts/compile-kernel.sh` | Compile kernel .c/.asm → .elf |
| `build/scripts/build-iso.sh` | grub-mkrescue → .iso |
| `build/scripts/build-disk.sh` | Create FAT disk image with OS binaries |
| `build/scripts/build-user-lib.sh` | Compile a user library |
| `build/scripts/build-user-app.sh` | Compile a user application |
| `build/scripts/build-os-command.sh` | Compile an OS command |
| `build/scripts/build-all.sh` | Orchestrate full build |
| `build/scripts/test-runner.sh` | Run test suite inside container |

---

## Phases

### Phase 1: Create `start.sh` and `start.ps1`

1. **`start.sh`** (macOS/Linux) — Single entry point that:
   - Detects platform (macOS vs Linux)
   - Checks for Docker and QEMU
   - If missing, prompts user and runs `scripts/setup-{macos,linux}.sh`
   - Calls `scripts/build.sh` (which docker-runs the toolchain)
   - Calls `scripts/boot.sh` (launches host QEMU)
2. **`start.ps1`** (Windows) — Same flow:
   - Checks for Docker and QEMU
   - If missing, prompts user and runs `scripts/setup-windows.ps1`
   - Calls `scripts/build.ps1`
   - Calls `scripts/boot.ps1`
3. Both scripts support `--setup-only`, `--build-only`, `--graphics`, `--skip-setup` flags.

### Phase 2: Create platform setup scripts

### Phase 2: Create platform setup scripts

1. **`scripts/setup-windows.ps1`** — Check/install Docker Desktop (winget), check/install QEMU (winget or choco), verify both are functional.
2. **`scripts/setup-macos.sh`** — Refactor existing script to focus only on Docker + QEMU (remove clang/nasm/etc. — those live in the container now).
3. **`scripts/setup-linux.sh`** — Install docker-ce + qemu-system-x86 via package manager, add user to docker group, verify.
4. All setup scripts end with a verification step that confirms:
   - `docker run hello-world` succeeds
   - `qemu-system-x86_64 --version` works

### Phase 3: Create unified build scripts

1. **`scripts/build.sh`** (macOS/Linux) — Ensures toolchain image exists (builds if needed), then `docker run -v $ROOT:/workspace` with the appropriate internal script.
2. **`scripts/build.ps1`** (Windows) — Same logic in PowerShell.
3. **Refactor `build/scripts/`** — Remove Docker-detection boilerplate from all internal scripts. They now unconditionally assume they're in the container.
4. **Rename/reorganize internal scripts** for clarity (e.g., `build_kernel_entry.sh` → `compile-kernel.sh`).

### Phase 4: Create unified boot scripts

1. **`scripts/boot.sh`** (macOS/Linux) — Invokes host QEMU with the appropriate `.args` file and built artifacts.
2. **`scripts/boot.ps1`** (Windows) — Same in PowerShell.
3. Both support `console` (headless, serial to stdio) and `graphics` (VGA window) modes.
4. Boot scripts validate that artifacts exist before launching (helpful error if you forgot to build).

### Phase 5: Clean up dead scripts

1. **Delete all `.ps1` files in `build/scripts/`** — no longer needed (host entry is `scripts/build.ps1`, internals are bash-in-container).
2. **Delete `.shell_parity_allowlist`**, `check_shell_parity.*`, `test_shell_parity.*` — obsolete.
3. **Delete `build/scripts/common.ps1`** — logic moves to `scripts/build.ps1`.
4. **Consolidate `os-*` wrappers** — evaluate if they're still needed or if the new `scripts/build.sh` subsumes them.
5. **Remove Docker-detection `if/else` blocks** from all remaining bash scripts.

### Phase 6: Consolidate test scripts

1. **Create `build/scripts/test-runner.sh`** — single container-internal entry point. Takes test name or `--all`.
2. **Keep individual `test_*.sh` files** as test definitions but strip Docker boilerplate.
3. **Add `scripts/test.sh` / `scripts/test.ps1`** — host-side wrappers that `docker run` the test-runner.
4. **Update CI workflows** to use `scripts/build.sh` and `scripts/test.sh`.

### Phase 7: Demo polish

1. **Rewrite `README.md`**:
   ```
   ## Quick Start

   git clone https://github.com/rwrife/SecureOS.git
   cd SecureOS

   # macOS / Linux
   ./start.sh

   # Windows (PowerShell)
   .\start.ps1

   That's it. The script installs Docker + QEMU if needed, builds the OS,
   and boots it in QEMU.
   ```
2. **Archive `BUILD_ROADMAP.md`** into `docs/`.
3. **Add `demo/` directory** with scripted demo scenarios.
4. **Ensure `artifacts/` is fully gitignored**.

---

## Files to Create

| File | Purpose |
|------|---------|
| `start.sh` | **Root entry point** — clone → run (macOS/Linux) |
| `start.ps1` | **Root entry point** — clone → run (Windows) |
| `scripts/setup-windows.ps1` | Windows dependency installer |
| `scripts/setup-linux.sh` | Linux dependency installer |
| `scripts/build.sh` | macOS/Linux host build entry point |
| `scripts/build.ps1` | Windows host build entry point |
| `scripts/boot.sh` | macOS/Linux host QEMU launcher |
| `scripts/boot.ps1` | Windows host QEMU launcher |
| `scripts/test.sh` | macOS/Linux host test entry point |
| `scripts/test.ps1` | Windows host test entry point |

## Files to Delete

### Dead / obsolete files (can be removed immediately)

| File | Reason |
|------|--------|
| `implementation_plan.md` (root) | Superseded by `plans/` directory; only referenced as historical context in bearssl scripts |
| `.gitkeep` (root) | Repo has content; no longer needed |
| `experiments/bootloader/boot.asm` | Legacy boot-sector experiment; real boot path is `kernel/arch/x86/boot/entry.asm` |
| `experiments/bootloader/boot_fail.asm` | Legacy experiment; tested via `test_boot_sector_fail.sh` but that test can use a generated stub |
| `scripts/setup-macos.sh` | Replaced by new `scripts/setup-macos.sh` (Docker+QEMU only, not full native toolchain) |

### Dead under new architecture (remove during consolidation)

| File/Pattern | Reason |
|--------------|--------|
| `build/scripts/*.ps1` (all ~20 files) | Replaced by `scripts/build.ps1` delegating to Docker |
| `build/scripts/.shell_parity_allowlist` | No longer relevant |
| `build/scripts/check_shell_parity.*` | No longer relevant |
| `build/scripts/test_shell_parity.*` | No longer relevant |
| `build/scripts/os-*.ps1` | Subsumed by `scripts/build.ps1` |
| `build/scripts/common.ps1` | Logic moves to `scripts/build.ps1` |
| `build/scripts/_os_wrapper_common.sh` | Only used by `os-*` wrappers being consolidated |
| `build/scripts/os-build` | Consolidated into `scripts/build.sh` |
| `build/scripts/os-package` | Consolidated into build pipeline |
| `build/scripts/os-run-qemu` | Consolidated into `scripts/boot.sh` |
| `build/scripts/os-snapshot` | Evaluate if still needed; if so, becomes container-internal |
| `build/scripts/os-validate` | Consolidated into `scripts/test.sh` |

## Files to Modify

| File | Change |
|------|--------|
| `scripts/setup-macos.sh` | Remove native compiler toolchain; keep Docker + QEMU only |
| `build/scripts/build_kernel_entry.sh` | Remove Docker-detection; assume in-container |
| `build/scripts/build_kernel_image.sh` | Remove Docker-detection; assume in-container |
| `build/scripts/build.sh` | Remove Docker-detection; becomes container-internal orchestrator |
| `build/scripts/test.sh` | Remove Docker-detection |
| `build/scripts/run_qemu.sh` | Remove (QEMU runs on host via `scripts/boot.sh`) |
| `README.md` | Rewrite quick start for 3-platform Docker flow |
| `.github/workflows/*.yml` | Use `scripts/build.sh` and `scripts/test.sh` |
| `CONTRIBUTING.md` | Update build/setup instructions |

---

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| QEMU location | **Host** | Needs display/device access; Docker can't easily forward GPU/display portably |
| Build toolchain | **Docker only** | Deterministic, no "works on my machine" issues |
| Script pairing | **`.sh` + `.ps1` for host scripts only** | Only ~4 pairs to maintain (setup, build, boot, test) instead of 100+ |
| Internal scripts | **Bash only, no `.ps1` peers** | They run inside the Linux container — PowerShell irrelevant there |
| Container image management | **Auto-build on first run** | `scripts/build.sh` checks for image, builds from Dockerfile if missing |
| Test execution | **Inside container** (no QEMU needed for unit tests) | QEMU integration tests use `scripts/boot.sh` separately |

---

## Success Criteria

- [ ] `./start.sh` or `.\start.ps1` works on a fresh clone (installs deps, builds, boots)
- [ ] Individual scripts (`scripts/build.sh`, `scripts/boot.sh`) work standalone for iterative dev
- [ ] `scripts/setup-{windows,macos,linux}` works on a fresh machine
- [ ] No `.ps1` files remain in `build/scripts/` (internal scripts are bash-only)
- [ ] README quick start is "clone + one command"
- [ ] CI workflows pass using new script entry points
- [ ] Total host-side scripts: ~10 (start + 4 pairs) instead of current ~160
