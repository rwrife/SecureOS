# Code Signing: Master Certificate Build-Time Generation & Verification Caching

**Date:** 2026-05-25
**Status:** Implementing

## Problem

All externally-built binaries (user apps, OS commands, libraries built via
`build_user_app.sh`, `build_user_lib.sh`, `build_os_command.sh`) were deployed
**unsigned** because the build scripts did not pass `--sign-key` / `--sign-cert`
to `sof_wrap`.  Only kernel-built binaries (constructed in `fs_service.c` at
boot) were signed, using the deterministic seeds from `root_key.h`.

Additionally, every binary load triggered a full Ed25519 signature verification
with no caching — wasteful for binaries loaded repeatedly.

## Solution

### 1. Build-time key generation (`tools/keygen` + `generate_keys.sh`)

A `keygen` host tool derives the root and intermediate keypairs from the
deterministic seeds in `root_key.h` and outputs:

- `artifacts/keys/root.pub` — root Ed25519 public key (32 bytes)
- `artifacts/keys/intermediate.seed` — intermediate signing seed (32 bytes)
- `artifacts/keys/intermediate.cert` — SCRT certificate (132 bytes)

`build/scripts/generate_keys.sh` invokes this tool early in the build pipeline.

### 2. All build scripts sign binaries

`build_user_app.sh`, `build_user_lib.sh`, and `build_os_command.sh` now detect
the presence of `artifacts/keys/intermediate.{seed,cert}` and pass them to
`sof_wrap` via `--sign-key` / `--sign-cert`.

### 3. Root cert deployed to `/certs` on disk

`build_disk_image.sh` copies `artifacts/keys/root.pub` → `/certs/root.pub`
on the disk image via a new `--certs-dir` flag in `populate_disk_image.py`.
This allows runtime validation against the master certificate.

### 4. Verification result cache in `launcher_exec.c`

A 32-slot hash table caches signature verification results, keyed on file path
and guarded by `total_size` for staleness detection.  Once a binary passes
verification, subsequent loads skip the expensive Ed25519 verify.

## Files Changed

| File | Change |
|------|--------|
| `tools/keygen/main.c` | New — key generation tool |
| `tools/keygen/Makefile` | New — build for keygen |
| `build/scripts/generate_keys.sh` | New — build step |
| `build/scripts/build.sh` | Call `generate_keys.sh` early |
| `build/scripts/build_user_app.sh` | Pass signing args to sof_wrap |
| `build/scripts/build_user_lib.sh` | Pass signing args to sof_wrap |
| `build/scripts/build_os_command.sh` | Pass signing args to sof_wrap |
| `build/scripts/build_disk_image.sh` | Deploy root cert to /certs |
| `tools/populate_disk_image.py` | Add `--certs-dir` support |
| `kernel/user/launcher_exec.c` | Add verification cache |
