#!/usr/bin/env bash
# build/scripts/test_proc_sched_aspace_invariant.sh
#
# Build + run the M1 scheduler aspace-invariant regression test
# (issue #260 done-when 3 — saved-stack-out-of-window panic site).
#
# Covers:
#   - Clean path: well-formed partitioned aspace dispatches without
#     invoking the panic hook.
#   - Deny: stack_top past the window's upper bound triggers
#     `stack_top_escapes_window` and force-exits the PCB.
#   - Deny: NULL aspace triggers `null_aspace`.
#   - Deny: ipc_scratch straddling the upper boundary triggers
#     `ipc_scratch_escapes_window`.
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
  "$ROOT_DIR/tests/proc_sched_aspace_invariant_test.c" \
  -o "$OUT_DIR/proc_sched_aspace_invariant_test"

LOG_PATH="$OUT_DIR/proc_sched_aspace_invariant_test.log"
"$OUT_DIR/proc_sched_aspace_invariant_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:proc_sched_aspace_invariant_clean" "$LOG_PATH"
grep -q "TEST:PASS:proc_sched_aspace_invariant_stack_escape" "$LOG_PATH"
grep -q "TEST:PASS:proc_sched_aspace_invariant_null_aspace" "$LOG_PATH"
grep -q "TEST:PASS:proc_sched_aspace_invariant_scratch_escape" "$LOG_PATH"
grep -q "TEST:PASS:proc_sched_aspace_invariant$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
