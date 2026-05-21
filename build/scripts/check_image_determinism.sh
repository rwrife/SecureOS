#!/usr/bin/env bash
# check_image_determinism.sh
#
# Builds the SecureOS disk image (and ISO) twice from a clean tree and asserts
# the two sha256 sums match. On mismatch, prints a debuggable diff summary
# (`cmp -l | head`, byte counts, optionally `diffoscope` when present).
#
# Per BUILD_ROADMAP.md §4.4 (artifacts/runs/<id>/) and §6.3 (deterministic
# artifact hashes for release candidates). See docs/build/determinism.md.
#
# Behavior:
#   - Honors $SECUREOS_RUN_ID (mints one if unset) so the resulting
#     image.sha256 lands inside the per-run artifact bundle (#161).
#   - Default target is the seeded disk image (`build/scripts/build.sh disk`)
#     because §4.4 names `secureos-disk.img`. Override via
#     $SECUREOS_DET_TARGET=image to check the kernel ISO instead, or
#     `both` to check both.
#   - Exits 0 on match, non-zero on mismatch. Intended to run with
#     `continue-on-error: true` in CI for the initial baseline (per the
#     issue #174 contract) until known-bad sources of non-determinism
#     are fixed in follow-up issues.
#
# Out of scope: actually *fixing* non-determinism (timestamps in FAT,
# unsorted directory entries, embedded build paths, random padding, etc.).
# This script makes the problem visible and measurable.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

TARGET="${SECUREOS_DET_TARGET:-disk}"

# Mint a run id if our parent (validate_bundle.sh, run_qemu.sh, CI) didn't.
if [[ -z "${SECUREOS_RUN_ID:-}" ]]; then
  short_sha="$(git rev-parse --short=12 HEAD 2>/dev/null || echo nogit)"
  ts="$(date -u +%Y%m%dT%H%M%SZ)"
  export SECUREOS_RUN_ID="${ts}-${short_sha}"
fi

RUN_DIR="$ROOT_DIR/artifacts/runs/$SECUREOS_RUN_ID"
mkdir -p "$RUN_DIR"

# Map logical target -> built artifact path.
artifact_path_for() {
  case "$1" in
    disk)  echo "$ROOT_DIR/artifacts/disk/secureos-disk.img" ;;
    image) echo "$ROOT_DIR/artifacts/kernel/secureos.iso" ;;
    *)     echo "" ;;
  esac
}

build_one() {
  local what="$1"
  # Clean to the maximum extent we can without nuking the toolchain cache.
  rm -rf artifacts/iso artifacts/kernel artifacts/disk artifacts/os artifacts/tests >/dev/null 2>&1 || true
  case "$what" in
    disk)  ./build/scripts/build.sh disk ;;
    image) ./build/scripts/build.sh image ;;
    *)     echo "[determinism] unknown target: $what" >&2; return 64 ;;
  esac
}

check_one_target() {
  local what="$1"
  local artifact
  artifact="$(artifact_path_for "$what")"
  if [[ -z "$artifact" ]]; then
    echo "[determinism] unknown target: $what" >&2
    return 64
  fi

  echo "[determinism] === target=$what ==="
  echo "[determinism] build #1"
  build_one "$what"
  if [[ ! -f "$artifact" ]]; then
    echo "[determinism] build #1 did not produce $artifact" >&2
    return 65
  fi
  local h1
  h1="$(sha256sum "$artifact" | awk '{print $1}')"
  local copy1="$RUN_DIR/${what}.first.bin"
  cp "$artifact" "$copy1"
  echo "[determinism] build #1 sha256=$h1"

  echo "[determinism] build #2"
  build_one "$what"
  if [[ ! -f "$artifact" ]]; then
    echo "[determinism] build #2 did not produce $artifact" >&2
    return 65
  fi
  local h2
  h2="$(sha256sum "$artifact" | awk '{print $1}')"
  local copy2="$RUN_DIR/${what}.second.bin"
  cp "$artifact" "$copy2"
  echo "[determinism] build #2 sha256=$h2"

  # Always emit the per-target hash record into the run bundle.
  # File name pattern: image.sha256 (the canonical name per #174), but we
  # disambiguate when checking multiple targets so neither overwrites the
  # other.
  local hash_file
  if [[ "$what" == "disk" ]]; then
    hash_file="$RUN_DIR/image.sha256"
  else
    hash_file="$RUN_DIR/${what}.sha256"
  fi
  {
    echo "$h1  build1/$(basename "$artifact")"
    echo "$h2  build2/$(basename "$artifact")"
  } > "$hash_file"

  if [[ "$h1" == "$h2" ]]; then
    echo "[determinism] PASS target=$what sha256=$h1"
    # Replace the two-line record with a single canonical line on success.
    echo "$h1  $(basename "$artifact")" > "$hash_file"
    rm -f "$copy1" "$copy2"
    return 0
  fi

  echo "[determinism] FAIL target=$what build1=$h1 build2=$h2" >&2
  echo "[determinism] sizes: $(stat -c '%s' "$copy1") vs $(stat -c '%s' "$copy2")" >&2
  echo "[determinism] first 32 differing bytes (offset old new):" >&2
  cmp -l "$copy1" "$copy2" 2>/dev/null | head -n 32 >&2 || true
  if command -v diffoscope >/dev/null 2>&1; then
    echo "[determinism] diffoscope summary:" >&2
    diffoscope --max-text-report-size 4096 --no-progress "$copy1" "$copy2" 2>&1 | head -n 200 >&2 || true
  else
    echo "[determinism] (install diffoscope locally for a richer summary)" >&2
  fi
  return 1
}

overall_rc=0
case "$TARGET" in
  both)
    check_one_target disk  || overall_rc=$?
    check_one_target image || overall_rc=$?
    ;;
  disk|image)
    check_one_target "$TARGET" || overall_rc=$?
    ;;
  *)
    echo "[determinism] SECUREOS_DET_TARGET must be one of: disk | image | both (got: $TARGET)" >&2
    exit 64
    ;;
esac

echo "[determinism] run bundle: $RUN_DIR"
ls -1 "$RUN_DIR" >&2 || true
exit "$overall_rc"
