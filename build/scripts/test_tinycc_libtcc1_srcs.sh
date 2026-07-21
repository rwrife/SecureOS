#!/usr/bin/env bash
# test_tinycc_libtcc1_srcs.sh — Host drift gate for TinyCC runtime-helper
# source partition (issue #548, umbrella #408 / #403).
#
# Contract:
#   - vendor/tinycc/libtcc1-srcs.json exists and parses.
#   - Every `required` / `excluded` entry exists in vendor/tinycc/tinycc/lib.
#   - required ∪ excluded exactly equals all compilable lib TUs (*.c + *.S).
#   - Summary counts remain pinned.
#
# This is the runtime-helper counterpart to:
#   - tinycc_vendor_gate   (pins compiler-core source set),
#   - tinycc_config_secureos (pins config knobs),
#   - tinycc_libc_deps     (pins libc call-surface partition).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PY="${PYTHON:-python3}"
MANIFEST_REL="vendor/tinycc/libtcc1-srcs.json"
MANIFEST_ABS="$ROOT_DIR/$MANIFEST_REL"
LIB_DIR="$ROOT_DIR/vendor/tinycc/tinycc/lib"

if [[ -f "$MANIFEST_ABS" ]]; then
  echo "TEST:PASS:tinycc_libtcc1_srcs_manifest_present"
else
  echo "TEST:FAIL:tinycc_libtcc1_srcs_manifest_present:missing:$MANIFEST_REL"
  echo "TEST:FAIL:tinycc_libtcc1_srcs:manifest_missing"
  exit 1
fi

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "TEST:SKIP:tinycc_libtcc1_srcs_manifest_parses:no_python3_on_path"
  echo "TEST:SKIP:tinycc_libtcc1_srcs_partition_entries_exist:no_python3_on_path"
  echo "TEST:SKIP:tinycc_libtcc1_srcs_partition_covers_all_tus:no_python3_on_path"
  echo "TEST:SKIP:tinycc_libtcc1_srcs_summary_counts_match:no_python3_on_path"
  echo "TEST:PASS:tinycc_libtcc1_srcs"
  exit 0
fi

if [[ ! -d "$LIB_DIR" ]] || [[ -z "$(ls -A "$LIB_DIR" 2>/dev/null)" ]]; then
  echo "TEST:SKIP:tinycc_libtcc1_srcs_manifest_parses:tinycc_submodule_not_initialized"
  echo "TEST:SKIP:tinycc_libtcc1_srcs_partition_entries_exist:tinycc_submodule_not_initialized"
  echo "TEST:SKIP:tinycc_libtcc1_srcs_partition_covers_all_tus:tinycc_submodule_not_initialized"
  echo "TEST:SKIP:tinycc_libtcc1_srcs_summary_counts_match:tinycc_submodule_not_initialized"
  echo "TEST:PASS:tinycc_libtcc1_srcs"
  exit 0
fi

TMP_LOG="$(mktemp)"
trap 'rm -f "$TMP_LOG"' EXIT

set +e
"$PY" "$ROOT_DIR/tools/validate_tinycc_libtcc1_srcs.py" \
  --root "$ROOT_DIR" \
  --manifest "$MANIFEST_REL" \
  --lib-dir "vendor/tinycc/tinycc/lib" \
  >"$TMP_LOG" 2>&1
RC=$?
set -e

cat "$TMP_LOG"

if [[ "$RC" -ne 0 ]]; then
  echo "TEST:FAIL:tinycc_libtcc1_srcs:validator_failed"
  exit 1
fi

echo "TEST:PASS:tinycc_libtcc1_srcs"
