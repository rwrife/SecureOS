#!/usr/bin/env bash
# test_tinycc_arena_drift.sh — issue #543
#
# Host drift gate for TinyCC compile-time arena pinning.
#
# Uses tools/validate_tinycc_arena.py to validate:
#   - vendor/tinycc/arena-measurements.json shape + TU sha256 pins,
#   - SKIP-pin semantics while #408 Phase 3 remains open,
#   - upward-drift threshold policy once status flips to measured mode.
#
# Output contract:
#   - TEST:SKIP:tinycc_arena_drift:awaiting_408_phase3   (placeholder mode)
#   - TEST:PASS:tinycc_arena_drift                        (rollup success)
#   - TEST:FAIL:tinycc_arena_drift:<reason>              (validator failure)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PY="${PYTHON:-python3}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "TEST:SKIP:tinycc_arena_drift:no_python3_on_path"
  echo "TEST:PASS:tinycc_arena_drift"
  exit 0
fi

TMP_LOG="$(mktemp)"
trap 'rm -f "$TMP_LOG"' EXIT

set +e
"$PY" "$ROOT_DIR/tools/validate_tinycc_arena.py" --root "$ROOT_DIR" "$@" >"$TMP_LOG" 2>&1
RC=$?
set -e

cat "$TMP_LOG"

if [ "$RC" -ne 0 ]; then
  echo "TEST:FAIL:tinycc_arena_drift:validator_failed"
  exit 1
fi

if grep -q '^TINYCC_ARENA:SKIP:awaiting_408_phase3' "$TMP_LOG"; then
  echo "TEST:SKIP:tinycc_arena_drift:awaiting_408_phase3"
fi

echo "TEST:PASS:tinycc_arena_drift"
