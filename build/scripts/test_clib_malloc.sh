#!/usr/bin/env bash
# build/scripts/test_clib_malloc.sh
#
# Build + run the userland heap allocator host unit test
# (issue #404, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P1).
#
# Covers:
#   - alloc / free / realloc / calloc round-trip
#   - boundary-tag coalescing
#   - brk-driven arena growth (callback path that the on-target build
#     will wire to os_mem_brk in the kernel-side follow-up)
#   - out-of-arena failure returns NULL (no panic)
#   - `toolchain_heap_isolation`: second-run isolation case from the
#     M7 plan — re-initialising the allocator yields byte-identical
#     placement and stats.
#   - clib_malloc_min_seed_bytes contract pinned.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/clib/src/malloc.c" \
  "$ROOT_DIR/tests/clib_malloc_test.c" \
  -o "$OUT_DIR/clib_malloc_test"

LOG_PATH="$OUT_DIR/clib_malloc_test.log"
"$OUT_DIR/clib_malloc_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_malloc:basic_roundtrip" "$LOG_PATH"
grep -q "TEST:PASS:clib_malloc:realloc_growth_and_shrink" "$LOG_PATH"
grep -q "TEST:PASS:clib_malloc:calloc_zeroes" "$LOG_PATH"
grep -q "TEST:PASS:clib_malloc:coalesce_neighbours" "$LOG_PATH"
grep -q "TEST:PASS:clib_malloc:brk_growth" "$LOG_PATH"
grep -q "TEST:PASS:clib_malloc:out_of_arena_no_panic" "$LOG_PATH"
grep -q "TEST:PASS:clib_malloc:toolchain_heap_isolation" "$LOG_PATH"
grep -q "TEST:PASS:clib_malloc:min_seed_bytes_anchored" "$LOG_PATH"
grep -q "TEST:PASS:clib_malloc$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
