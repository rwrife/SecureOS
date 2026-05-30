#!/usr/bin/env bash
# build/scripts/test_clib_stdbool.sh
#
# Build + run the freestanding <stdbool.h> nucleus host unit test
# (issue #407 slice 9, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Compiled with -fno-builtin so the assertions exercise OUR header
# macros + helper TU rather than __builtin_* shortcuts. Mirrors the
# build shape of test_clib_string.sh, test_clib_ctype.sh,
# test_clib_qsort.sh, and test_clib_limits.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/stdbool.c" \
  "$ROOT_DIR/tests/clib_stdbool_test.c" \
  -o "$OUT_DIR/clib_stdbool_test"

LOG_PATH="$OUT_DIR/clib_stdbool_test.log"
"$OUT_DIR/clib_stdbool_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_stdbool:true_false_values"        "$LOG_PATH"
grep -q "TEST:PASS:clib_stdbool:bool_is_underscore_bool"  "$LOG_PATH"
grep -q "TEST:PASS:clib_stdbool:feature_macro"            "$LOG_PATH"
grep -q "TEST:PASS:clib_stdbool:usable_in_control_flow"   "$LOG_PATH"
grep -q "TEST:PASS:clib_stdbool:symbol_set_pinned"        "$LOG_PATH"
grep -q "TEST:PASS:clib_stdbool$"                         "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
