#!/usr/bin/env bash
# Host gate for issue #595: pin libmanifestgen default runtime.arena_bytes.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"
BIN="$OUT_DIR/manifestgen_default_arena_bytes_test"
LOG="$OUT_DIR/manifestgen_default_arena_bytes_test.log"
VALIDATOR="$ROOT_DIR/build/scripts/validate_manifestgen_default_arena.sh"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/manifestgen/src/manifest_default.c" \
  "$ROOT_DIR/tests/manifestgen/default_arena_bytes_test.c" \
  -o "$BIN"

"$BIN" | tee "$LOG"

grep -q '^TEST:PASS:manifestgen_default_arena_bytes$' "$LOG"
! grep -q '^TEST:FAIL:' "$LOG"

if [[ ! -r "$VALIDATOR" ]]; then
  echo "TEST:FAIL:harness_missing_script:$VALIDATOR" >&2
  exit 78
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
VLOG="$TMP/manifestgen_default_arena_validator.log"

set +e
bash "$VALIDATOR" >"$VLOG" 2>&1
RC=$?
set -e

cat "$VLOG"

if [[ "$RC" -ne 0 ]]; then
  echo "TEST:FAIL:manifestgen_default_arena:validator_failed" >&2
  exit 1
fi

grep -q 'MANIFESTGEN_DEFAULT_ARENA:PASS:summary:' "$VLOG"

echo "TEST:PASS:manifestgen_default_arena"
