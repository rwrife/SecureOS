#!/usr/bin/env bash
# build/scripts/test_clib_inttypes.sh
#
# Build + run the freestanding <inttypes.h> nucleus host unit test
# (issue #407 slice 11, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Compiled with -fno-builtin so the assertions exercise OUR header
# macros rather than __builtin_* shortcuts. Mirrors the build shape
# of test_clib_stdint.sh, test_clib_stddef.sh, test_clib_string.sh,
# and test_clib_float.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/inttypes.c" \
  "$ROOT_DIR/user/libs/clib/src/stdlib.c" \
  "$ROOT_DIR/user/libs/clib/src/errno.c" \
  "$ROOT_DIR/tests/clib_inttypes_test.c" \
  -o "$OUT_DIR/clib_inttypes_test"

LOG_PATH="$OUT_DIR/clib_inttypes_test.log"
"$OUT_DIR/clib_inttypes_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_inttypes:macro_shape_pinned"        "$LOG_PATH"
grep -q "TEST:PASS:clib_inttypes:printf_roundtrip_pinned"   "$LOG_PATH"
grep -q "TEST:PASS:clib_inttypes:least_fast_format_pinned"  "$LOG_PATH"
grep -q "TEST:PASS:clib_inttypes:max_ptr_roundtrip_pinned"  "$LOG_PATH"
grep -q "TEST:PASS:clib_inttypes:symbol_set_pinned"         "$LOG_PATH"
grep -q "TEST:PASS:clib_inttypes:imaxabs_imaxdiv_pinned"    "$LOG_PATH"
grep -q "TEST:PASS:clib_inttypes:strto_imax_umax_pinned"    "$LOG_PATH"
grep -q "TEST:PASS:clib_inttypes$"                          "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
