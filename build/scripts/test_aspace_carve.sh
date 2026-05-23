#!/usr/bin/env bash
# build/scripts/test_aspace_carve.sh
#
# Build + run the M1 address-space partitioning unit test
# (issue #248, plan plans/2026-05-20-m1-process-address-space.md
# slice 2).
#
# Covers:
#   - Equal-sized, non-overlapping, monotonically-increasing windows.
#   - stack_top + ipc_scratch land inside each window.
#   - pt_reserved is initialised to NULL.
#   - ASPACE_ERR_ARENA_TOO_SMALL when per-window < aspace_window_min_bytes().
#   - ASPACE_ERR_INVALID_ARG on NULL out / zero count.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh and validate_bundle.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/proc/address_space.c" \
  "$ROOT_DIR/tests/aspace_carve_test.c" \
  -o "$OUT_DIR/aspace_carve_test"

LOG_PATH="$OUT_DIR/aspace_carve_test.log"
"$OUT_DIR/aspace_carve_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:aspace_carve_partition_layout" "$LOG_PATH"
grep -q "TEST:PASS:aspace_carve_arena_too_small" "$LOG_PATH"
grep -q "TEST:PASS:aspace_carve_invalid_arg" "$LOG_PATH"
grep -q "TEST:PASS:aspace_carve_min_window_bytes" "$LOG_PATH"
grep -q "TEST:PASS:aspace_carve$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
