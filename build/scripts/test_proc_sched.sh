#!/usr/bin/env bash
# Build + run the M1 cooperative scheduler + IPC block/wake acceptance
# test (issue #250, plan plans/2026-05-20-m1-process-address-space.md
# slice 3).
#
# Covers:
#   - Single-PCB dispatch + exit_code recording.
#   - Two-PCB cooperative yield interleave (round-robin).
#   - ipc_recv blocks until matching ipc_send wakes the waiter; payload
#     and kernel-stamped sender_subject reach the receiver intact.
#   - ipc_send blocks on a full single-waiter slot until the receiver
#     consumes the staged envelope.
#   - Deadlock detection: PROC_SCHED_ERR_DEADLOCK instead of hang when
#     every live PCB is BLOCKED.
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
  "$ROOT_DIR/kernel/proc/address_space.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/kernel/proc/proc_sched.c" \
  "$ROOT_DIR/kernel/ipc/ipc_port.c" \
  "$ROOT_DIR/kernel/ipc/ipc_ops.c" \
  "$ROOT_DIR/tests/proc_sched_test.c" \
  -o "$OUT_DIR/proc_sched_test"

LOG_PATH="$OUT_DIR/proc_sched_test.log"
"$OUT_DIR/proc_sched_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:proc_sched_single_dispatch"          "$LOG_PATH"
grep -q "TEST:PASS:proc_sched_round_robin"              "$LOG_PATH"
grep -q "TEST:PASS:proc_sched_recv_blocks_until_send"   "$LOG_PATH"
grep -q "TEST:PASS:proc_sched_send_blocks_on_full"      "$LOG_PATH"
grep -q "TEST:PASS:proc_sched_deadlock_detected"        "$LOG_PATH"
grep -q "TEST:PASS:proc_sched$"                         "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
