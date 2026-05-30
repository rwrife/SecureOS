#!/usr/bin/env bash
# build/scripts/test_clib_stddef.sh
#
# Build + run the freestanding <stddef.h> nucleus host unit test
# (issue #407 slice 9, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Compiled with -fno-builtin so the assertions exercise OUR header
# typedefs / macros rather than __builtin_* shortcuts. Mirrors the
# build shape of test_clib_limits.sh, test_clib_string.sh,
# test_clib_ctype.sh, and test_clib_qsort.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/stddef.c" \
  "$ROOT_DIR/tests/clib_stddef_test.c" \
  -o "$OUT_DIR/clib_stddef_test"

LOG_PATH="$OUT_DIR/clib_stddef_test.log"
"$OUT_DIR/clib_stddef_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_stddef:null_defined"          "$LOG_PATH"
grep -q "TEST:PASS:clib_stddef:typedef_widths_pinned" "$LOG_PATH"
grep -q "TEST:PASS:clib_stddef:max_align_t_pinned"    "$LOG_PATH"
grep -q "TEST:PASS:clib_stddef:offsetof_works"        "$LOG_PATH"
grep -q "TEST:PASS:clib_stddef:symbol_set_pinned"     "$LOG_PATH"
grep -q "TEST:PASS:clib_stddef$"                      "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
