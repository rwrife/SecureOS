#!/usr/bin/env bash
# build/scripts/test_clib_bsearch.sh
#
# Build + run the freestanding bsearch host unit test
# (issue #407 slice 7, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Compiled with -fno-builtin so the assertions exercise OUR bsearch
# implementation rather than __builtin_bsearch / a hosted libc shortcut.
# Mirrors the build shape of test_clib_qsort.sh / test_clib_string.sh /
# test_clib_ctype.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/bsearch.c" \
  "$ROOT_DIR/tests/clib_bsearch_test.c" \
  -o "$OUT_DIR/clib_bsearch_test"

LOG_PATH="$OUT_DIR/clib_bsearch_test.log"
"$OUT_DIR/clib_bsearch_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_bsearch:empty_returns_null"                  "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:single_hit"                          "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:single_miss"                         "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:hit_at_first"                        "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:hit_at_last"                         "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:hit_in_middle"                       "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:miss_below_range"                    "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:miss_above_range"                    "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:miss_between_neighbours"             "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:duplicates_returns_some_match"       "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:struct_elements_payload_intact"      "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:odd_size_unaligned_elements"         "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:large_array_no_overflow"             "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:defensive_null_key"                  "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:defensive_null_compar"               "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:defensive_zero_size"                 "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:defensive_null_base_nonzero_nmemb"   "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch:symbol_set_pinned"                   "$LOG_PATH"
grep -q "TEST:PASS:clib_bsearch$"                                    "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
