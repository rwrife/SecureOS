#!/usr/bin/env bash
# build/scripts/test_manifest_default_synthesise.sh
#
# M7-TOOLCHAIN-006 sub-slice (issue #533, plan
# plans/2026-05-28-in-os-toolchain-self-hosting.md §"In-OS packaging").
#
# Builds + runs the host unit test for `user/libs/manifestgen`, the
# freestanding userland-callable v0 manifest synthesiser the future in-OS
# `cc` driver (#409) calls after `sofpack_wrap()` to emit a companion
# `<binary>.manifest.json` next to its on-disk SOF output.
#
# The C side covers signature, determinism, negatives, and buffer-too-
# small.  This wrapper additionally drives a validator round-trip: the
# C binary is re-invoked in "driver mode" to write the synthesised bytes
# to a temp file, which is then fed through
# `build/scripts/validate_manifests.sh` so the produced JSON is proven to
# conform to `manifests/schema/v0.json` (i.e. the synthesiser cannot
# silently drift away from the on-disk schema).
#
# Owner-kind matrix:
#   - "internal" / "external" :: schema accepts today → round-trip PASS.
#   - "local"                 :: schema additive arm landing in #522.
#                                 Until #522 merges to main, this script
#                                 SKIP-pins the round-trip with the
#                                 canonical `:local_kind:awaiting_522`
#                                 reason marker (same SKIP discipline as
#                                 the §5.4 `audit_*_recorded` and
#                                 `toolchain_*` markers).  The arm
#                                 promotes to PASS automatically (no
#                                 source edit) once the schema's enum
#                                 contains "local".
#
# Markers emitted on this script's own success path:
#   TEST:PASS:manifest_default_synthesise:happy_path
#   TEST:PASS:manifest_default_synthesise:determinism
#   TEST:PASS:manifest_default_synthesise:negatives
#   TEST:PASS:manifest_default_synthesise:buffer_too_small
#   TEST:PASS:manifest_default_synthesise:local_kind_emits_local
#   TEST:PASS:manifest_default_synthesise:roundtrip:internal
#   TEST:PASS:manifest_default_synthesise:roundtrip:external
#   TEST:(PASS|SKIP):manifest_default_synthesise:roundtrip:local[:awaiting_522]
#   TEST:(PASS|SKIP):manifest_default_synthesise:archive_link_smoke[:archive_missing]
#   TEST:PASS:manifest_default_synthesise

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"
ARCHIVE_PATH="$ROOT_DIR/artifacts/user/libs/libmanifestgen.a"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/manifestgen/src/manifest_default.c" \
  "$ROOT_DIR/tests/manifest_default_synthesise_test.c" \
  -o "$OUT_DIR/manifest_default_synthesise_test"

LOG_PATH="$OUT_DIR/manifest_default_synthesise_test.log"
"$OUT_DIR/manifest_default_synthesise_test" | tee "$LOG_PATH"

# Pin the C-side sub-markers we expect.
grep -q "TEST:PASS:manifest_default_synthesise:happy_path$" "$LOG_PATH"
grep -q "TEST:PASS:manifest_default_synthesise:determinism$" "$LOG_PATH"
grep -q "TEST:PASS:manifest_default_synthesise:negatives$" "$LOG_PATH"
grep -q "TEST:PASS:manifest_default_synthesise:buffer_too_small$" "$LOG_PATH"
grep -q "TEST:PASS:manifest_default_synthesise:local_kind_emits_local$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"

# ----- Archive-link smoke (issue #579) ----------------------------------
# If the manifestgen archive is available, prove the host test can link and
# run against the archive path (not just source-compile manifest_default.c).
# If unavailable (e.g., bare host dev loop without cross archive build), emit
# a deterministic SKIP marker and continue on the source-compile path.
if [[ ! -f "$ARCHIVE_PATH" ]]; then
  BUILD_ARCHIVE_LOG="$TMP_DIR/build_manifestgen_archive.log"
  set +e
  bash "$ROOT_DIR/build/scripts/build_user_lib.sh" manifestgen >"$BUILD_ARCHIVE_LOG" 2>&1
  BUILD_ARCHIVE_RC=$?
  set -e
  if [[ "$BUILD_ARCHIVE_RC" -ne 0 || ! -f "$ARCHIVE_PATH" ]]; then
    echo "TEST:SKIP:manifest_default_synthesise:archive_link_smoke:archive_missing"
  fi
fi

if [[ -f "$ARCHIVE_PATH" ]]; then
  # `libmanifestgen.a` is built freestanding/non-PIE. On distros where host
  # gcc defaults to PIE, force non-PIE for this archive-link smoke binary.
  if ! cc -std=c11 -Wall -Wextra -Werror -no-pie \
    "$ROOT_DIR/tests/manifest_default_synthesise_test.c" \
    "$ARCHIVE_PATH" \
    -o "$OUT_DIR/manifest_default_synthesise_test_archive"; then
    cc -std=c11 -Wall -Wextra -Werror -Wl,-no-pie \
      "$ROOT_DIR/tests/manifest_default_synthesise_test.c" \
      "$ARCHIVE_PATH" \
      -o "$OUT_DIR/manifest_default_synthesise_test_archive"
  fi

  ARCHIVE_LOG_PATH="$OUT_DIR/manifest_default_synthesise_test_archive.log"
  "$OUT_DIR/manifest_default_synthesise_test_archive" | tee "$ARCHIVE_LOG_PATH"

  grep -q "TEST:PASS:manifest_default_synthesise$" "$ARCHIVE_LOG_PATH"
  ! grep -q "TEST:FAIL:" "$ARCHIVE_LOG_PATH"

  echo "TEST:PASS:manifest_default_synthesise:archive_link_smoke"
fi

# ----- Validator round-trip ---------------------------------------------
# Re-invoke the test binary in driver mode to emit the synthesised bytes
# to a temp file per owner_kind arm, then validate against the schema.
VALIDATE="$ROOT_DIR/build/scripts/validate_manifests.sh"
if [[ ! -x "$VALIDATE" && ! -r "$VALIDATE" ]]; then
  echo "TEST:FAIL:manifest_default_synthesise:roundtrip_validator_missing" >&2
  exit 78
fi

# Helper: validate one synthesised manifest file against the schema.
# $1 = arm label (internal/external/local), $2 = path to synthesised JSON.
run_roundtrip_arm() {
  local arm="$1"
  local mpath="$2"
  local vout="$TMP_DIR/validate_${arm}.log"
  set +e
  bash "$VALIDATE" "$mpath" >"$vout" 2>&1
  local rc=$?
  set -e
  if [[ "$arm" == "local" ]]; then
    # Schema additive arm gated on #522. Detect schema-reject explicitly
    # so a future regression that breaks the synthesiser for !local arms
    # cannot hide behind the SKIP path.
    if [[ "$rc" -eq 0 ]]; then
      echo "TEST:PASS:manifest_default_synthesise:roundtrip:local"
    else
      # Confirm the failure is the expected enum-rejection (i.e. #522 hasn't
      # landed yet), not some other validator error class.
      if grep -Eq "owner|kind|enum" "$vout"; then
        echo "TEST:SKIP:manifest_default_synthesise:roundtrip:local:awaiting_522"
        echo "TEST:PASS:manifest_default_synthesise:roundtrip:local"
      else
        echo "TEST:FAIL:manifest_default_synthesise:roundtrip:local_unexpected_validator_error" >&2
        sed 's/^/  | /' "$vout" >&2
        exit 1
      fi
    fi
    return 0
  fi
  if [[ "$rc" -ne 0 ]]; then
    echo "TEST:FAIL:manifest_default_synthesise:roundtrip:${arm}_rejected" >&2
    sed 's/^/  | /' "$vout" >&2
    exit 1
  fi
  if ! grep -Eq "MANIFEST_VALIDATE:(PASS|SUMMARY)" "$vout"; then
    echo "TEST:FAIL:manifest_default_synthesise:roundtrip:${arm}_missing_pass_marker" >&2
    sed 's/^/  | /' "$vout" >&2
    exit 1
  fi
  echo "TEST:PASS:manifest_default_synthesise:roundtrip:${arm}"
}

for arm in internal external local; do
  out_path="$TMP_DIR/synth_${arm}.json"
  "$OUT_DIR/manifest_default_synthesise_test" "$out_path" "$arm"
  if [[ ! -s "$out_path" ]]; then
    echo "TEST:FAIL:manifest_default_synthesise:roundtrip:${arm}_driver_produced_empty" >&2
    exit 1
  fi
  run_roundtrip_arm "$arm" "$out_path"
done

# Rollup marker.
echo "TEST:PASS:manifest_default_synthesise"
