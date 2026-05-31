#!/usr/bin/env bash
# build/scripts/test_clib_assert.sh
#
# Build + run the freestanding <assert.h> host unit test
# (issue #407 / M7-TOOLCHAIN-004, plan
#  plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/assert.c" \
  "$ROOT_DIR/tests/clib_assert_test.c" \
  -o "$OUT_DIR/clib_assert_test"

LOG_PATH="$OUT_DIR/clib_assert_test.log"
"$OUT_DIR/clib_assert_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_assert:macro_defined"                       "$LOG_PATH"
grep -q "TEST:PASS:clib_assert:feature_macro"                       "$LOG_PATH"
grep -q "TEST:PASS:clib_assert:static_assert_works"                 "$LOG_PATH"
grep -q "TEST:PASS:clib_assert:assert_pass_no_call"                 "$LOG_PATH"
grep -q "TEST:PASS:clib_assert:assert_pass_no_side_eff_when_ndebug" "$LOG_PATH"
grep -q "TEST:PASS:clib_assert:assert_fail_invokes_handler"         "$LOG_PATH"
grep -q "TEST:PASS:clib_assert:handler_null_restores_default"       "$LOG_PATH"
grep -q "TEST:PASS:clib_assert:reinclude_toggles_ndebug"            "$LOG_PATH"
grep -q "TEST:PASS:clib_assert:symbol_set_pinned"                   "$LOG_PATH"
grep -q "TEST:PASS:clib_assert$"                                    "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
