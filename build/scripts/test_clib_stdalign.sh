#!/usr/bin/env bash
# build/scripts/test_clib_stdalign.sh
#
# Build + run the freestanding <stdalign.h> nucleus host unit test
# (issue #407 slice, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Compiled with -fno-builtin so the assertions exercise OUR header
# macros + helper TU rather than any compiler intrinsics. Mirrors the
# build shape of test_clib_string.sh, test_clib_ctype.sh,
# test_clib_qsort.sh, test_clib_iso646.sh, and the other clib slice
# scripts.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/stdalign.c" \
  "$ROOT_DIR/tests/clib_stdalign_test.c" \
  -o "$OUT_DIR/clib_stdalign_test"

LOG_PATH="$OUT_DIR/clib_stdalign_test.log"
"$OUT_DIR/clib_stdalign_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_stdalign:macros_defined"           "$LOG_PATH"
grep -q "TEST:PASS:clib_stdalign:macros_expand_correctly"  "$LOG_PATH"
grep -q "TEST:PASS:clib_stdalign:feature_test_macros"      "$LOG_PATH"
grep -q "TEST:PASS:clib_stdalign:helper_tu_agrees"         "$LOG_PATH"
grep -q "TEST:PASS:clib_stdalign:symbol_set_pinned"        "$LOG_PATH"
grep -q "TEST:PASS:clib_stdalign$"                         "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
