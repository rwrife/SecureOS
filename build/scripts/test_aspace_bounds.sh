#!/usr/bin/env bash
# build/scripts/test_aspace_bounds.sh
#
# Build + run the M1 address-space bounds-check unit test (issue #260,
# plan plans/2026-05-20-m1-process-address-space.md slice 2).
#
# Covers:
#   - In-window pointer + length passes.
#   - One byte past base+size is rejected.
#   - Range straddling the upper or lower boundary is rejected.
#   - NULL aspace returns false.
#   - Overflow-safe rejection of windows / ranges whose arithmetic
#     would wrap past UINTPTR_MAX.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh and validate_bundle.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/proc/address_space.c" \
  "$ROOT_DIR/tests/aspace_bounds_test.c" \
  -o "$OUT_DIR/aspace_bounds_test"

LOG_PATH="$OUT_DIR/aspace_bounds_test.log"
"$OUT_DIR/aspace_bounds_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:aspace_bounds_allow" "$LOG_PATH"
grep -q "TEST:PASS:aspace_bounds_deny" "$LOG_PATH"
grep -q "TEST:PASS:aspace_bounds_straddle" "$LOG_PATH"
grep -q "TEST:PASS:aspace_bounds_null_aspace" "$LOG_PATH"
grep -q "TEST:PASS:aspace_bounds_overflow" "$LOG_PATH"
grep -q "TEST:PASS:aspace_bounds$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
