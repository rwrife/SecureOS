#!/usr/bin/env bash
# build/scripts/test_clib_stdint.sh
#
# Build + run the freestanding <stdint.h> nucleus host unit test
# (issue #407 slice 10, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Compiled with -fno-builtin so the assertions exercise OUR header
# typedefs / macros rather than __builtin_* shortcuts. Mirrors the
# build shape of test_clib_stddef.sh, test_clib_limits.sh,
# test_clib_string.sh, test_clib_ctype.sh, and test_clib_qsort.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/stdint.c" \
  "$ROOT_DIR/tests/clib_stdint_test.c" \
  -o "$OUT_DIR/clib_stdint_test"

LOG_PATH="$OUT_DIR/clib_stdint_test.log"
"$OUT_DIR/clib_stdint_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_stdint:exact_widths_pinned"      "$LOG_PATH"
grep -q "TEST:PASS:clib_stdint:pointer_widths_pinned"    "$LOG_PATH"
grep -q "TEST:PASS:clib_stdint:max_widths_pinned"        "$LOG_PATH"
grep -q "TEST:PASS:clib_stdint:limits_pinned"            "$LOG_PATH"
grep -q "TEST:PASS:clib_stdint:size_and_ptrdiff_pinned"  "$LOG_PATH"
grep -q "TEST:PASS:clib_stdint:const_macros_pinned"      "$LOG_PATH"
grep -q "TEST:PASS:clib_stdint:symbol_set_pinned"        "$LOG_PATH"
grep -q "TEST:PASS:clib_stdint$"                         "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
