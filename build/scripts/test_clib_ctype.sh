#!/usr/bin/env bash
# build/scripts/test_clib_ctype.sh
#
# Build + run the freestanding ctype family host unit test
# (issue #407 slice 2, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Compiled with -fno-builtin so the assertions exercise OUR ctype
# implementations rather than __builtin_isdigit / __builtin_toupper etc.
# Mirrors the build shape of test_clib_string.sh and test_clib_malloc.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/ctype.c" \
  "$ROOT_DIR/tests/clib_ctype_test.c" \
  -o "$OUT_DIR/clib_ctype_test"

LOG_PATH="$OUT_DIR/clib_ctype_test.log"
"$OUT_DIR/clib_ctype_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_ctype:isascii_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:isdigit_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:isxdigit_full_range" "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:isalpha_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:isalnum_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:isspace_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:isblank_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:isupper_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:islower_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:iscntrl_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:isprint_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:isgraph_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:ispunct_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:toupper_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:tolower_full_range"  "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:eof_safe"            "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype:symbol_set_pinned"   "$LOG_PATH"
grep -q "TEST:PASS:clib_ctype$"                    "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
