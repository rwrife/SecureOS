# SecureOS Build Wrappers

Canonical wrapper scripts for deterministic local/agent workflows.

## Targets

```bash
./build/scripts/build.sh image
./build/scripts/build.sh run
./build/scripts/build.sh test-boot
```

## Direct test/run

```bash
./build/scripts/test.sh hello_boot
./build/scripts/run_qemu.sh --test hello_boot
```

## Notes

- Scripts are intended to run through the pinned toolchain container (`secureos/toolchain:bookworm-2026-02-12`).
- If the image is missing, `build.sh` bootstraps it from `build/docker/Dockerfile.toolchain`.
- Deterministic QEMU flags live in `build/qemu/x86_64-headless.args`.
- QEMU logs are written to `artifacts/qemu/<test>.log`.
- Per-run metadata is written to `artifacts/qemu/<test>.meta.json`.
