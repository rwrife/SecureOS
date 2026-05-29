#!/usr/bin/env bash
# build/scripts/test_clib_qsort.sh
#
# Build + run the freestanding qsort host unit test
# (issue #407 / M7-TOOLCHAIN-004 slice 3, plan
#  plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Covers:
#   - empty / single-element no-op contract
#   - already-sorted idempotence
#   - reverse-sorted pathological case (median-of-three + iterative
#     stack keep us in O(log n) depth — no stack overflow on N=2048)
#   - random integer permutation
#   - heavy duplicates (grouped correctly)
#   - small-range insertion-sort fallback (n=2..7)
#   - struct elements (byte-wise swap moves whole struct)
#   - 1-byte and 3-byte (odd / unaligned) element widths
#   - model comparison vs an O(n^2) insertion-sort on a
#     pseudorandom workload (canonical sorted permutation)
#   - symbol_set_pinned drift marker (parity with str/mem PR #416
#     and ctype PR #417)
#
# Compiled with `-fno-builtin` so the assertions exercise OUR qsort,
# not `__builtin_qsort` / the host libc.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/qsort.c" \
  "$ROOT_DIR/tests/clib_qsort_test.c" \
  -o "$OUT_DIR/clib_qsort_test"

LOG_PATH="$OUT_DIR/clib_qsort_test.log"
"$OUT_DIR/clib_qsort_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_qsort:empty_no_op" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort:single_no_op" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort:sorted_idempotent" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort:reverse_sorted" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort:random_ints" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort:duplicates_grouped" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort:small_under_insertion_threshold" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort:large_pathological_no_overflow" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort:struct_elements" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort:byte_elements_size_one" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort:odd_size_unaligned_elements" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort:stable_against_model" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort:symbol_set_pinned" "$LOG_PATH"
grep -q "TEST:PASS:clib_qsort$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
