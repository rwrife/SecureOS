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

### Known sources (live punchlist)

Current baseline (2026-07-21): **no divergent regions observed** in the
most recent `Image determinism check` on `main`
([Actions run 29791737944](https://github.com/rwrife/SecureOS/actions/runs/29791737944),
artifact `runs-artifacts`, ID `8480538133`).

Classification table from that run:

| Root-cause class | Regions observed | Tracking issue |
| --- | --- | --- |
| `grub_timestamp` | 0 | _none_ |
| `fat_mtime` | 0 | _none_ |
| `embedded_build_path` | 0 | _none_ |
| `unsorted_dir_entries` | 0 | _none_ |
| `random_padding` | 0 | _none_ |
| `other:*` | 0 | _none_ |

When this check regresses, open one sibling issue per distinct root cause
using the naming convention `determinism: fix <root-cause> — <artifact>:<offset-range>`
and link those issues here.

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
