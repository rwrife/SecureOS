#!/usr/bin/env bash
# build/scripts/test_clib_stdlib.sh
#
# Build + run the freestanding stdlib subset host unit test
# (M7-TOOLCHAIN-004 slice 4, issue #407, plan
# plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Compiled with -fno-builtin so the assertions exercise OUR atoi /
# strtol / strtoul / abs / labs implementations rather than the host
# libc / compiler builtins. Mirrors the build shape of
# test_clib_string.sh / test_clib_ctype.sh / test_clib_malloc.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/stdlib.c" \
  "$ROOT_DIR/user/libs/clib/src/errno.c" \
  "$ROOT_DIR/tests/clib_stdlib_test.c" \
  -o "$OUT_DIR/clib_stdlib_test"

LOG_PATH="$OUT_DIR/clib_stdlib_test.log"
"$OUT_DIR/clib_stdlib_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_stdlib:atoi_basic"                "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtol_decimal"            "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtol_hex_octal_auto"     "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtol_endptr_no_digits"   "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtol_overflow_clamp"     "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtol_invalid_base"       "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtoul_basic"             "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtoul_overflow_clamp"    "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtoll_decimal"           "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtoll_overflow_clamp"    "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtoll_endptr_no_digits"  "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtoll_invalid_base"      "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtoull_basic"            "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:strtoull_overflow_clamp"   "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:errno_overflow_set_erange"  "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:errno_invalid_base_set_einval" "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:errno_success_preserved"   "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:abs_labs"                  "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib:symbol_set_pinned"         "$LOG_PATH"
grep -q "TEST:PASS:clib_stdlib$"                          "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
