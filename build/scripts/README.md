# SecureOS Internal Build Scripts

These scripts run **inside** the Docker toolchain container. They are not
intended to be called directly from the host — use the host-side entry
points in `scripts/` instead.

## Entry Points (host-side)

```bash
# From repo root:
./start.sh                          # Setup + build + boot (one command)
./scripts/build.sh [kernel|disk|all]  # Build inside container
./scripts/boot.sh [console|graphics]  # Launch QEMU on host
./scripts/test.sh [test_name|--all]   # Run tests in container
```

## Internal Scripts (container-side)

| Script | Purpose |
|--------|---------|
| `build.sh` | Orchestrator — routes targets to sub-scripts |
| `build_kernel_entry.sh` | Compile kernel asm/C → kernel.elf |
| `build_kernel_image.sh` | Create bootable ISO via grub-mkrescue |
| `build_disk_image.sh` | Build FAT disk image with OS binaries |
| `build_bearssl.sh` | Compile BearSSL for freestanding x86-64 |
| `build_user_lib.sh` | Compile a user library → SOF .lib |
| `build_user_app.sh` | Compile a user app → SOF .bin |
| `build_os_command.sh` | Compile an OS command → SOF .bin |
| `test.sh` | Test dispatcher — routes named tests |
| `run_qemu.sh` | QEMU launcher (used by test harness) |

## Artifacts

- `artifacts/kernel/kernel.elf` — Linked kernel binary
- `artifacts/kernel/secureos.iso` — Bootable GRUB ISO
- `artifacts/disk/secureos-disk.img` — FAT disk image
- `artifacts/user/` — User app binaries
- `artifacts/lib/` — User library binaries
- `artifacts/os/` — OS command binaries

