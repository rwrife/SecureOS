# Incremental Build Optimization

**Date:** 2026-05-25  
**Status:** Proposed  
**Goal:** Make the default `start.ps1` / `start.sh` automatically detect what changed and only rebuild those layers — no manual flags required.

## Problem

Running `start.ps1` / `start.sh` always invokes `build.sh all`, which:

1. Spins up a new Docker container every invocation
2. Regenerates signing keys (idempotent but still a check)
3. Rebuilds BearSSL from source (~100+ object files)
4. Recompiles the entire kernel (~40 .c files + assembly)
5. Creates a new ISO via grub-mkrescue
6. Rebuilds all user libs and apps
7. Rebuilds the full disk image from scratch

For iterative app development, steps 1–5 are unnecessary if kernel source
hasn't changed.

## Solution: Automatic Change Detection + Smart Rebuild

### Core Idea: Build Manifest + Source Fingerprinting

After each successful build, record a **build manifest** at
`artifacts/.build-manifest` containing the md5/sha256 hash of every source
file that contributed to each layer. On the next build, compare the current
source hashes against the manifest to determine which layers are stale.

### Build Layers (dependency order)

```
Layer 0: keys          (artifacts/keys/)        ← from build/scripts/generate_keys.sh
Layer 1: bearssl       (artifacts/bearssl/)     ← from vendor/bearssl/**
Layer 2: kernel        (artifacts/kernel/)      ← from kernel/**/*.{c,h,asm}
Layer 3: iso           (artifacts/kernel/*.iso) ← from kernel.elf + grub.cfg
Layer 4: user libs     (artifacts/lib/)         ← from user/libs/**
Layer 5: user apps     (artifacts/user/)        ← from user/apps/** + user/include/**
Layer 6: os commands   (artifacts/os/)          ← from user/os_commands/**
Layer 7: disk image    (artifacts/disk/)        ← from layers 4-6 outputs
```

### Detection Logic (`build/scripts/detect_changes.sh`)

```bash
# For each layer, define its source glob:
KERNEL_SOURCES="kernel/**/*.c kernel/**/*.h kernel/**/*.asm kernel/arch/x86/boot/linker.ld"
BEARSSL_SOURCES="vendor/bearssl/**/*.c vendor/bearssl/**/*.h"
LIB_SOURCES="user/libs/**/*.c user/libs/**/*.h user/include/**"
APP_SOURCES="user/apps/**/*.c user/apps/**/*.h user/include/**"
OS_CMD_SOURCES="user/os_commands/**"

# Compute current hash of each source set
# Compare against stored hash in artifacts/.build-manifest
# Return which layers need rebuilding
```

The script outputs a space-separated list like: `apps disk` or `kernel iso apps libs disk` or `none`.

### How `build.sh all` changes (smart mode)

```bash
STALE=$(./build/scripts/detect_changes.sh)

if [[ "$STALE" == "none" ]]; then
  echo "[build] Everything up-to-date, nothing to rebuild"
  exit 0
fi

# Only rebuild stale layers (respecting dependency order)
[[ "$STALE" == *keys* ]]    && ./build/scripts/generate_keys.sh
[[ "$STALE" == *bearssl* ]] && ./build/scripts/build_bearssl.sh
[[ "$STALE" == *kernel* ]]  && ./build/scripts/build_kernel_entry.sh
[[ "$STALE" == *iso* ]]     && ./build/scripts/build_kernel_image.sh
[[ "$STALE" == *libs* ]]    && # rebuild libs...
[[ "$STALE" == *apps* ]]    && # rebuild apps...
[[ "$STALE" == *disk* ]]    && ./build/scripts/build_disk_image.sh

# Update manifest with new hashes
./build/scripts/update_manifest.sh
```

### Dependency Cascade Rules

If a lower layer is stale, higher layers that depend on it are also stale:

- `kernel` changed → also rebuild `iso`
- `user/include/**` changed → rebuild `libs` + `apps` + `disk`
- `libs` changed → rebuild `disk` (apps may link against libs)
- `apps` changed → rebuild `disk`
- `bearssl` changed → rebuild `libs` (netlib) + `apps` (those using netlib) + `disk`

### Manual Override Flags (still available)

For cases where you *know* what you want:

| Flag | Effect |
|------|--------|
| `--force` / `-Force` | Ignore manifest, rebuild everything |
| `--app <name>` / `-App <name>` | Force-rebuild one app + repack disk |
| `--kernel` | Force kernel rebuild only |

But the **default behavior** (no flags) is now smart — it auto-detects.

### Build Manifest Format (`artifacts/.build-manifest`)

```ini
[meta]
timestamp=2026-05-25T16:00:00Z
build_id=abc123

[keys]
hash=sha256:abcdef...

[bearssl]
hash=sha256:123456...

[kernel]
hash=sha256:789abc...

[libs]
hash=sha256:def012...

[apps]
hash=sha256:345678...

[os_commands]
hash=sha256:9abcde...
```

Each `hash` is a combined hash of all source files in that layer (sorted,
concatenated, then hashed). This is fast to compute (~50ms for the whole tree)
and deterministic.

## Implementation Order

1. **Create `build/scripts/detect_changes.sh`** — computes per-layer source
   hashes and compares against manifest
2. **Create `build/scripts/update_manifest.sh`** — writes manifest after
   successful build
3. **Modify `build/scripts/build.sh`** — `all` target uses detect_changes
   to skip unchanged layers; add cascade logic
4. **Add `--force` flag** to host scripts for clean-rebuild escape hatch
5. **Add `--app <name>` flag** for targeted single-app rebuild
6. **Update help text** in all scripts

## Expected Results

| Scenario | Before | After |
|----------|--------|-------|
| Full clean build (no manifest) | ~60-90s | ~60-90s (same, builds everything) |
| No source changes | ~60-90s | ~2-3s (detects nothing stale, skips all) |
| Only app code edited | ~60-90s | ~5-10s (rebuilds app + repacks disk) |
| Only kernel code edited | ~60-90s | ~20-30s (rebuilds kernel + ISO, skips apps) |
| Shared header edited | ~60-90s | ~15-20s (rebuilds libs + apps + disk) |

## Files Created/Modified

- `build/scripts/detect_changes.sh` — **new**, source fingerprinting + comparison
- `build/scripts/update_manifest.sh` — **new**, writes manifest after build
- `build/scripts/build.sh` — modified, smart `all` target
- `scripts/build.sh` — modified, pass `--force` flag through
- `scripts/build.ps1` — modified, pass `-Force` flag through
- `start.sh` — modified, add `--force` and `--app` flags
- `start.ps1` — modified, add `-Force` and `-App` flags
- `artifacts/.build-manifest` — generated at build time (gitignored)
