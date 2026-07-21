#!/usr/bin/env bash
# build/scripts/test_manifestgen_negative.sh
#
# Issue #592 negative-contract gate for libmanifestgen:
#   - malformed inputs map to stable error/reason markers
#   - output buffer remains untouched on any non-zero return
#   - out_len remains unchanged on any non-zero return
#   - implementation remains allocation-free (no malloc/calloc/realloc/free)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"
mkdir -p "$OUT_DIR"

BIN="$OUT_DIR/manifestgen_negative_test"
LOG="$OUT_DIR/manifestgen_negative_test.log"
OBJ="$OUT_DIR/manifest_default_for_negative_test.o"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/manifestgen/src/manifest_default.c" \
  "$ROOT_DIR/tests/manifestgen_negative_test.c" \
  -o "$BIN"

"$BIN" | tee "$LOG"

grep -q "TEST:PASS:manifestgen_negative:owner_kind_invalid$" "$LOG"
grep -q "TEST:PASS:manifestgen_negative:arena_above_max$" "$LOG"
grep -q "TEST:PASS:manifestgen_negative:arena_size_max$" "$LOG"
grep -q "TEST:PASS:manifestgen_negative:caps_required_too_many$" "$LOG"
grep -q "TEST:PASS:manifestgen_negative:path_empty$" "$LOG"
grep -q "TEST:PASS:manifestgen_negative:output_too_small$" "$LOG"
grep -q "TEST:PASS:manifestgen_negative$" "$LOG"
! grep -q "TEST:FAIL:" "$LOG"

# Symbol-level pin: manifest_default.c remains allocation-free.
cc -std=c11 -Wall -Wextra -Werror -c \
  "$ROOT_DIR/user/libs/manifestgen/src/manifest_default.c" \
  -o "$OBJ"

if nm -u "$OBJ" | grep -Eq '\b(malloc|calloc|realloc|free)\b'; then
  echo "TEST:FAIL:manifestgen_negative:allocation_symbols_present" >&2
  nm -u "$OBJ" >&2
  exit 1
fi

echo "TEST:PASS:manifestgen_negative:allocation_free"
