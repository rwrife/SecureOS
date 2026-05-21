# Build determinism (BUILD_ROADMAP §4.4 / §6.3)

This document defines the **deterministic-image contract** used by CI and by
the `os-snapshot` agent tool wrapper (#162).

## Goal

Given a clean working tree at commit `C`, building the SecureOS disk image
twice must produce **bit-identical** output:

```
sha256(build1/secureos-disk.img) == sha256(build2/secureos-disk.img)
```

Without this guarantee we cannot distinguish a real regression from a
non-determinism bug (FAT timestamps, unsorted directory entries, embedded
build paths, random padding, etc.), and snapshots taken by `os-snapshot`
are not comparable across runs.

## Tooling

- **Script:** `build/scripts/check_image_determinism.sh`
  (PowerShell peer: `check_image_determinism.ps1`, per AGENTS.md
  cross-platform rule).
- **Targets:** `disk` (default, `secureos-disk.img`), `image`
  (`secureos.iso`), or `both`. Override with
  `SECUREOS_DET_TARGET=image` or `=both`.
- **Bundle integration (#161):** result is recorded as
  `artifacts/runs/<SECUREOS_RUN_ID>/image.sha256` (for the disk target),
  or `<target>.sha256` for the ISO target. The script honors a parent's
  `SECUREOS_RUN_ID`; if unset, it mints one with the same
  `YYYYMMDDTHHMMSSZ-<shortsha>` rule as `validate_bundle.sh` and
  `run_qemu.sh`.
- **On match:** the hash file is one line, `sha256  filename`.
- **On mismatch:** the hash file records both sums, the two binaries are
  preserved in the run bundle as `<target>.first.bin` /
  `<target>.second.bin`, and the script prints the first 32 differing
  byte offsets via `cmp -l | head` plus an optional `diffoscope`
  summary when that tool is available.

## CI wiring

CI runs the determinism check as a **separate, currently non-blocking**
step inside the `iso-vm-build` workflow (`continue-on-error: true`),
per issue #174:

> Initial run is allowed to fail — log the result as a known-bad
> baseline issue rather than blocking merges — but the job itself is
> wired in and visible in PR checks.

The step uploads `artifacts/runs/` alongside the existing kernel / disk /
qemu artifact bundles so a failing run leaves both `first.bin` and
`second.bin` downloadable for offline diffing.

Each known source of non-determinism gets its **own** follow-up issue
(grub timestamp, FAT mtime, embedded build path, etc.). When those are
fixed, the `continue-on-error: true` flag is removed and the check
becomes a release-candidate gate per BUILD_ROADMAP §6.3.

## Local usage

```bash
# Default: check the disk image.
./build/scripts/check_image_determinism.sh

# Check both the disk image and the kernel ISO.
SECUREOS_DET_TARGET=both ./build/scripts/check_image_determinism.sh

# Run inside a named bundle.
SECUREOS_RUN_ID=det-smoke ./build/scripts/check_image_determinism.sh
ls artifacts/runs/det-smoke/
# image.sha256                (on match)
# disk.first.bin  disk.second.bin  image.sha256  (on mismatch)
```

## Out of scope

- *Fixing* the sources of non-determinism. The purpose of this script is
  to make the problem visible and measurable; fixes are tracked in
  separate issues per source.
- Reproducibility of the kernel ELF in isolation (separate, easier; can
  be added later with the same script extended to a `kernel` target).

## Cross-references

- BUILD_ROADMAP.md §4.4 (per-run artifact bundle)
- BUILD_ROADMAP.md §6.3 (deterministic artifact hashes for release candidates)
- docs/test-plans/artifact-bundle.md (run bundle layout, #161)
- Issue #161 (per-run bundle layout)
- Issue #162 (`os-snapshot` consumes the same hash)
- Issue #174 (this contract)
