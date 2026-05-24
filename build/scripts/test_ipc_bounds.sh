#!/usr/bin/env bash
# build/scripts/test_ipc_bounds.sh
#
# Build + run the end-to-end IPC address_space bounds enforcement test
# (issue #260 — IPC half of the runtime bounds check, on top of the
# aspace_contains helper / process_find_aspace_by_subject lookup).
#
# Covers:
#   - allow: in-window send + recv succeeds with a live PCB+aspace.
#   - deny: one-past-end envelope returns IPC_ERR_BOUNDS and emits
#     exactly one CAP:DENY:<sender>:ipc_send:bounds marker.
#   - deny: envelope straddling the upper boundary is also rejected.
#   - backward-compat: subjects with no live PCB skip the check, so
#     ipc_sync_v0 / ipc_handle_gate harnesses keep working unchanged.
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
  "$ROOT_DIR/kernel/proc/address_space.c" \
  "$ROOT_DIR/kernel/proc/proc_sched.c" \
  "$ROOT_DIR/kernel/ipc/ipc_port.c" \
  "$ROOT_DIR/kernel/ipc/ipc_ops.c" \
  "$ROOT_DIR/tests/ipc_bounds_test.c" \
  -o "$OUT_DIR/ipc_bounds_test"

LOG_PATH="$OUT_DIR/ipc_bounds_test.log"
"$OUT_DIR/ipc_bounds_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:ipc_bounds_allow" "$LOG_PATH"
grep -q "TEST:PASS:ipc_bounds_deny_one_past_end" "$LOG_PATH"
grep -q "TEST:PASS:ipc_bounds_deny_straddle" "$LOG_PATH"
grep -q "TEST:PASS:ipc_bounds_no_pcb_skipped" "$LOG_PATH"
grep -q "TEST:PASS:ipc_bounds$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
