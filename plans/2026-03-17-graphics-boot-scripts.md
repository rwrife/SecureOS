# Graphics Boot Script Plan (2026-03-17)

## Goal
Add first-class scripts for launching SecureOS with a QEMU graphics window from host QEMU while preserving existing containerized build reproducibility.

## Scope
- Add `build/scripts/boot_graphics.ps1` for Windows.
- Add `build/scripts/boot_graphics.sh` for Linux/macOS.
- Add `graphics` target to top-level build wrappers.
- Document new usage in `build/scripts/README.md`.

## Design
1. Build Pipeline
- Reuse existing `build_kernel_image` and `build_disk_image` scripts before launch.
- Keep Docker/toolchain image bootstrap behavior unchanged.

2. Launch Behavior
- Use host-installed `qemu-system-x86_64` for graphics mode.
- Load deterministic args from `build/qemu/x86_64-graphical.args`.
- Preserve debug-exit handling for pass/fail semantics.

3. Compatibility
- Keep `boot_console` unchanged for headless serial workflows.
- Add `graphics` target in `build.sh` and `build.ps1`.

## Validation
- Confirm scripts parse and launch command path is valid.
- Verify build wrapper usage/help includes `graphics` target.

## Follow-up
- Optionally add explicit `-display` mode variants for SDL/GTK based on host platform.
- Optionally add a host-QEMU preflight script with install/version guidance.
