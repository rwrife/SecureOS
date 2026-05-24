#!/usr/bin/env bash
# Build + run the M1 process-table lifecycle test (issue #224).
#
# Covers:
#   - create → lookup snapshot preserves subject + aspace.
#   - destroy invalidates the PID (stale lookup, double-destroy,
#     destroy(PID_INVALID) all return PROC_ERR_INVALID_PID).
#   - create → destroy → create reuses the slot index but advances
#     the generation field (matches the #220 / #237 lifecycle pattern).
#   - PROC_TABLE_MAX fill → next create returns PROC_ERR_TABLE_FULL
#     and writes PID_INVALID to the out param.
#   - process_table_reset invalidates every previously issued PID.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh and validate_bundle.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/tests/process_table_test.c" \
  -o "$OUT_DIR/process_table_test"

LOG_PATH="$OUT_DIR/process_table_test.log"
"$OUT_DIR/process_table_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:process_table_create_lookup" "$LOG_PATH"
grep -q "TEST:PASS:process_table_destroy_semantics" "$LOG_PATH"
grep -q "TEST:PASS:process_table_pid_advances" "$LOG_PATH"
grep -q "TEST:PASS:process_table_full_rejected" "$LOG_PATH"
grep -q "TEST:PASS:process_table_reset_invalidates" "$LOG_PATH"
grep -q "TEST:PASS:process_table$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
