#!/usr/bin/env bash
# build/scripts/test_clib_stdarg.sh
#
# Build + run the freestanding <stdarg.h> host unit test
# (issue #407 / M7-TOOLCHAIN-004 slice 6, plan
#  plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Covers:
#   - va_list typedef present
#   - va_start / va_arg / va_end on a 5-int variadic walk
#   - va_copy: two parallel walks each yield independent sums
#   - zero / negative argcount paths (no UB)
#   - 64-arg walk crossing the x86_64 SysV register-spill boundary
#   - saturating overflow clamp on INT_MAX / INT_MIN
#   - symbol_set_pinned drift marker (parity with str/mem PR #416,
#     ctype PR #417, qsort PR #418, stdlib PR #428, errno PR #430)
#
# Compiled with `-fno-builtin` so the assertions exercise OUR macro
# expansions (which intentionally forward to __builtin_va_*) rather
# than any host-libc shortcut.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/stdarg.c" \
  "$ROOT_DIR/tests/clib_stdarg_test.c" \
  -o "$OUT_DIR/clib_stdarg_test"

LOG_PATH="$OUT_DIR/clib_stdarg_test.log"
"$OUT_DIR/clib_stdarg_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_stdarg:va_list_typedef_present"   "$LOG_PATH"
grep -q "TEST:PASS:clib_stdarg:va_start_arg_end_basic"    "$LOG_PATH"
grep -q "TEST:PASS:clib_stdarg:va_copy_independent_walks" "$LOG_PATH"
grep -q "TEST:PASS:clib_stdarg:zero_args_clean"           "$LOG_PATH"
grep -q "TEST:PASS:clib_stdarg:large_arg_count"           "$LOG_PATH"
grep -q "TEST:PASS:clib_stdarg:saturating_overflow_clamp" "$LOG_PATH"
grep -q "TEST:PASS:clib_stdarg:symbol_set_pinned"         "$LOG_PATH"
grep -q "TEST:PASS:clib_stdarg$"                          "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
