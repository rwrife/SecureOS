#!/usr/bin/env bash
# build/scripts/test_clib_string.sh
#
# Build + run the freestanding str*/mem* host unit test
# (issue #407 slice 1, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Covers:
#   - memcpy / memmove (forward + backward overlap)
#   - memset / memcmp (unsigned ordering) / memchr
#   - strlen / strnlen
#   - strcmp / strncmp
#   - strcpy / strncpy (historical zero-pad + truncation)
#   - strcat / strncat
#   - strchr / strrchr / strstr (incl. empty-needle + OOB-safety)
#   - strspn / strcspn / strpbrk (slice 12)
#   - strtok / strtok_r (slice 12, single-state + reentrant)
#   - symbol_set_pinned drift test
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh.
#
# Notes:
#   - Compile with -fno-builtin so memcpy / strlen etc. are NOT folded
#     to host-libc builtins; the test exercises the implementations in
#     user/libs/clib/src/string.c.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/string.c" \
  "$ROOT_DIR/tests/clib_string_test.c" \
  -o "$OUT_DIR/clib_string_test"

LOG_PATH="$OUT_DIR/clib_string_test.log"
"$OUT_DIR/clib_string_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_string:memcpy_basic"              "$LOG_PATH"
grep -q "TEST:PASS:clib_string:memmove_overlap_forward"   "$LOG_PATH"
grep -q "TEST:PASS:clib_string:memmove_overlap_backward"  "$LOG_PATH"
grep -q "TEST:PASS:clib_string:memset_fill"               "$LOG_PATH"
grep -q "TEST:PASS:clib_string:memcmp_order"              "$LOG_PATH"
grep -q "TEST:PASS:clib_string:memchr_hit_and_miss"       "$LOG_PATH"
grep -q "TEST:PASS:clib_string:strlen_and_strnlen"        "$LOG_PATH"
grep -q "TEST:PASS:clib_string:strcmp_order"              "$LOG_PATH"
grep -q "TEST:PASS:clib_string:strncmp_bounded"           "$LOG_PATH"
grep -q "TEST:PASS:clib_string:strcpy_and_strncpy_pad"    "$LOG_PATH"
grep -q "TEST:PASS:clib_string:strcat_and_strncat"        "$LOG_PATH"
grep -q "TEST:PASS:clib_string:strchr_and_strrchr"        "$LOG_PATH"
grep -q "TEST:PASS:clib_string:strstr_hit_and_miss"       "$LOG_PATH"
grep -q "TEST:PASS:clib_string:strspn_basic"              "$LOG_PATH"
grep -q "TEST:PASS:clib_string:strcspn_basic"             "$LOG_PATH"
grep -q "TEST:PASS:clib_string:strpbrk_hit_and_miss"      "$LOG_PATH"
grep -q "TEST:PASS:clib_string:strtok_walks_tokens"       "$LOG_PATH"
grep -q "TEST:PASS:clib_string:strtok_r_reentrant_independence" "$LOG_PATH"
grep -q "TEST:PASS:clib_string:symbol_set_pinned"         "$LOG_PATH"
grep -q "TEST:PASS:clib_string$"                          "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
