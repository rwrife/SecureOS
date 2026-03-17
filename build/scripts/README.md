# SecureOS Build Wrappers

Canonical wrapper scripts for deterministic local/agent workflows.

## Targets

```bash
./build/scripts/build.sh kernel
./build/scripts/build.sh image
./build/scripts/build.sh run
./build/scripts/build.sh test-boot
./build/scripts/build.sh user-app
./build/scripts/build.sh user-lib
./build/scripts/build.sh disk
./build/scripts/build.sh console
./build/scripts/build.sh graphics
./build/scripts/build_user_app.sh filedemo
./build/scripts/build_user_lib.sh envlib
./build/scripts/build_user_lib.sh fslib
./build/scripts/boot_console.sh
./build/scripts/boot_graphics.sh
```

PowerShell (Windows):

```powershell
.\build\scripts\build.ps1 kernel
.\build\scripts\build.ps1 image
.\build\scripts\build.ps1 run
.\build\scripts\build.ps1 test-boot
.\build\scripts\build.ps1 user-app
.\build\scripts\build.ps1 user-lib
.\build\scripts\build.ps1 disk
.\build\scripts\build.ps1 console
.\build\scripts\build.ps1 graphics
.\build\scripts\build_user_app.ps1 filedemo
.\build\scripts\build_user_lib.ps1 envlib
.\build\scripts\build_user_lib.ps1 fslib
.\build\scripts\boot_console.ps1
.\build\scripts\boot_graphics.ps1
```

## Direct test/run

```bash
./build/scripts/test.sh hello_boot
./build/scripts/test.sh scheduler
./build/scripts/test.sh app_runtime
./build/scripts/test.sh kernel_console
./build/scripts/test.sh kernel_filedemo
./build/scripts/test.sh kernel_persistence
./build/scripts/test.sh kernel_sessions
./build/scripts/run_qemu.sh --test hello_boot
./build/scripts/run_qemu.sh --test kernel_prompt
./build/scripts/run_qemu.sh --test kernel_console
./build/scripts/run_qemu.sh --test kernel_filedemo
./build/scripts/run_qemu.sh --test kernel_persistence
./build/scripts/validate_bundle.sh
```

PowerShell (Windows):

```powershell
.\build\scripts\test.ps1 hello_boot
.\build\scripts\test.ps1 scheduler
.\build\scripts\test.ps1 app_runtime
.\build\scripts\test.ps1 kernel_console
.\build\scripts\test.ps1 kernel_filedemo
.\build\scripts\test.ps1 kernel_persistence
.\build\scripts\test.ps1 kernel_sessions
.\build\scripts\run_qemu.ps1 -Test hello_boot
.\build\scripts\run_qemu.ps1 -Test kernel_console
.\build\scripts\run_qemu.ps1 -Test kernel_filedemo
.\build\scripts\run_qemu.ps1 -Test kernel_persistence
.\build\scripts\validate_bundle.ps1
```

## Notes

- Scripts are intended to run through the pinned toolchain container (`secureos/toolchain:bookworm-2026-02-12`).
- If the image is missing, `build.sh` bootstraps it from `build/docker/Dockerfile.toolchain`.
- Deterministic QEMU flags live in `build/qemu/x86_64-headless.args`.
- Deterministic graphical QEMU flags live in `build/qemu/x86_64-graphical.args`.
- QEMU logs are written to `artifacts/qemu/<test>.log`.
- Per-run metadata is written to `artifacts/qemu/<test>.meta.json`.
- The kernel ISO is written to `artifacts/kernel/secureos.iso`.
- The attached raw disk image is written to `artifacts/disk/secureos-disk.img`.
- In the kernel console, `storage` reports the active backend and geometry.
- `run_qemu.sh --test kernel_prompt` starts an interactive session at `secureos>`.
- Disk-image and interactive boot wrappers auto-stop stale SecureOS QEMU/container instances before launch.
- Windows wrappers execute the same bash harness inside the pinned toolchain container.
- User app artifacts are written to `artifacts/user/<app>.o` and `artifacts/user/<app>.elf`.
- User library artifacts are written to `artifacts/lib/<lib>.o` and `artifacts/lib/<lib>.elf`.
