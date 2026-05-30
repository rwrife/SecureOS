#!/usr/bin/env bash
# build/scripts/test_clib_setjmp.sh
#
# Build + run the freestanding <setjmp.h> host unit test
# (issue #407 / M7-TOOLCHAIN-004 slice 7, plan
#  plans/2026-05-28-in-os-toolchain-self-hosting.md P3, issue #446).
#
# Covers:
#   - setjmp/longjmp round-trip with non-zero val (returns val)
#   - longjmp(env, 0) coerced to setjmp() return value 1 (ISO C)
#   - callee-saved register restored to setjmp-time value
#   - jmp_buf can be reused after a longjmp
#   - symbol_set_pinned drift marker (parity with the other clib
#     slices: str/mem PR #416, ctype PR #417, qsort PR #418,
#     stdlib PR #428, errno PR #430, stdarg PR #431).
#
# Implementation under test is hand-rolled assembly
# (user/libs/clib/src/setjmp_x86.S) implementing the i386 and x86_64
# SysV callee-saved snapshot/restore. The host CI runs as
# x86_64-linux-gnu, so the x86_64 branch is what executes here; the
# i386 branch is what the on-target build (BUILD_ROADMAP §3) uses.
#
# Compiled with `-fno-builtin` so the assertions exercise OUR
# setjmp/longjmp pair rather than any host-libc shortcut.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/setjmp_x86.S" \
  "$ROOT_DIR/tests/clib_setjmp_test.c" \
  -o "$OUT_DIR/clib_setjmp_test"

LOG_PATH="$OUT_DIR/clib_setjmp_test.log"
"$OUT_DIR/clib_setjmp_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_setjmp:roundtrip_nonzero"      "$LOG_PATH"
grep -q "TEST:PASS:clib_setjmp:zero_coerced_to_one"    "$LOG_PATH"
grep -q "TEST:PASS:clib_setjmp:callee_saved_restored"  "$LOG_PATH"
grep -q "TEST:PASS:clib_setjmp:nested_env_reuse"       "$LOG_PATH"
grep -q "TEST:PASS:clib_setjmp:symbol_set_pinned"      "$LOG_PATH"
grep -q "TEST:PASS:clib_setjmp$"                       "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
