#!/usr/bin/env bash
# build/scripts/test_clib_float.sh
#
# Build + run the freestanding <float.h> nucleus host unit test
# (issue #407 slice, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Compiled with -fno-builtin so the assertions exercise OUR header
# macros + helper TU rather than any compiler intrinsics. Mirrors the
# build shape of test_clib_stdalign.sh, test_clib_string.sh,
# test_clib_ctype.sh, test_clib_qsort.sh and the other clib slice
# scripts.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/float.c" \
  "$ROOT_DIR/tests/clib_float_test.c" \
  -o "$OUT_DIR/clib_float_test"

LOG_PATH="$OUT_DIR/clib_float_test.log"
"$OUT_DIR/clib_float_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_float:macros_defined"        "$LOG_PATH"
grep -q "TEST:PASS:clib_float:c11_minima"            "$LOG_PATH"
grep -q "TEST:PASS:clib_float:helper_int_roundtrip"  "$LOG_PATH"
grep -q "TEST:PASS:clib_float:helper_fp_roundtrip"   "$LOG_PATH"
grep -q "TEST:PASS:clib_float:symbol_set_pinned"     "$LOG_PATH"
grep -q "TEST:PASS:clib_float$"                      "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
