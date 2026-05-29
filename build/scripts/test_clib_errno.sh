#!/usr/bin/env bash
# build/scripts/test_clib_errno.sh
#
# Build + run the freestanding <errno.h> host unit test
# (issue #407 / M7-TOOLCHAIN-004 slice 5, plan
#  plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Covers:
#   - macro_values_pinned: every shipped errno macro equals the pinned
#     literal value (musl / Linux numbering); drift trips here before
#     TinyCC starts depending on ERANGE / EINVAL / ENOMEM numbering.
#   - errno_global_zero_init: BSS contract — errno is 0 at start-up.
#   - errno_writable_roundtrip: writing through `errno` is observed on
#     the next read (asserts errno is a real lvalue, not a TLS macro).
#   - errno_address_stable: `&errno` is constant in this TU (drift
#     guard against an accidental __errno_location indirection).
#   - strerror_known_codes: every shipped errnum returns a non-NULL
#     bounded ASCII description from clib_strerror.
#   - strerror_unknown_code: out-of-range codes return the literal
#     "Unknown error" rather than NULL.
#   - symbol_set_pinned: drift marker (parity with the str/mem PR #416,
#     ctype PR #417, qsort PR #418, and stdlib PR #428 slices).
#
# Compiled with `-fno-builtin` so the assertions exercise OUR macros
# and global rather than __builtin_* / glibc's <errno.h>.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/errno.c" \
  "$ROOT_DIR/tests/clib_errno_test.c" \
  -o "$OUT_DIR/clib_errno_test"

LOG_PATH="$OUT_DIR/clib_errno_test.log"
"$OUT_DIR/clib_errno_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_errno:macro_values_pinned"      "$LOG_PATH"
grep -q "TEST:PASS:clib_errno:errno_global_zero_init"   "$LOG_PATH"
grep -q "TEST:PASS:clib_errno:errno_writable_roundtrip" "$LOG_PATH"
grep -q "TEST:PASS:clib_errno:errno_address_stable"     "$LOG_PATH"
grep -q "TEST:PASS:clib_errno:strerror_known_codes"     "$LOG_PATH"
grep -q "TEST:PASS:clib_errno:strerror_unknown_code"    "$LOG_PATH"
grep -q "TEST:PASS:clib_errno:symbol_set_pinned"        "$LOG_PATH"
grep -q "TEST:PASS:clib_errno$"                         "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
