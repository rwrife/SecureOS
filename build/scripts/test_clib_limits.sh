#!/usr/bin/env bash
# build/scripts/test_clib_limits.sh
#
# Build + run the freestanding <limits.h> nucleus host unit test
# (issue #407 slice 8, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Compiled with -fno-builtin so the assertions exercise OUR header
# macros + helper TU rather than __builtin_*_max shortcuts. Mirrors
# the build shape of test_clib_string.sh, test_clib_ctype.sh, and
# test_clib_qsort.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/limits.c" \
  "$ROOT_DIR/tests/clib_limits_test.c" \
  -o "$OUT_DIR/clib_limits_test"

LOG_PATH="$OUT_DIR/clib_limits_test.log"
"$OUT_DIR/clib_limits_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_limits:macro_values_pinned"   "$LOG_PATH"
grep -q "TEST:PASS:clib_limits:min_max_relation"      "$LOG_PATH"
grep -q "TEST:PASS:clib_limits:char_bit_eq_8"         "$LOG_PATH"
grep -q "TEST:PASS:clib_limits:char_is_signed"        "$LOG_PATH"
grep -q "TEST:PASS:clib_limits:symbol_set_pinned"     "$LOG_PATH"
grep -q "TEST:PASS:clib_limits$"                      "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
