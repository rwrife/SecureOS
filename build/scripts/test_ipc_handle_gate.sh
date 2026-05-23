#!/usr/bin/env bash
# Build + run the M1 handle-gated IPC tests (issue #246, M1-CAPTBL-006).
#
# Covers:
#   - allow path (ipc_send_h / ipc_recv_h with valid handles)
#   - deny path: handle for the wrong cap (CAP_CONSOLE_WRITE -> ipc_send_h)
#   - deny path: stale handle (granted then revoked)
#   - deny path: wrong owner on recv (handle owner != port owner)
#   - cap_handle_owner contract: stale handle -> owner == 0
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
  "$ROOT_DIR/kernel/proc/proc_sched.c" \
  "$ROOT_DIR/kernel/ipc/ipc_port.c" \
  "$ROOT_DIR/kernel/ipc/ipc_ops.c" \
  "$ROOT_DIR/tests/ipc_handle_gate_test.c" \
  -o "$OUT_DIR/ipc_handle_gate_test"

LOG_PATH="$OUT_DIR/ipc_handle_gate_test.log"
"$OUT_DIR/ipc_handle_gate_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:ipc_handle_gate_owner_accessor" "$LOG_PATH"
grep -q "TEST:PASS:ipc_handle_gate_allow"          "$LOG_PATH"
grep -q "TEST:PASS:ipc_handle_gate_deny_wrong_cap" "$LOG_PATH"
grep -q "TEST:PASS:ipc_handle_gate_deny_stale"     "$LOG_PATH"
grep -q "TEST:PASS:ipc_handle_gate_deny_wrong_owner" "$LOG_PATH"
grep -q "TEST:PASS:ipc_handle_gate"                "$LOG_PATH"
# Canonical deny markers per docs/abi/capability-deny-contract.md §4.
grep -Eq "^CAP:DENY:[0-9]+:ipc_send:-$" "$LOG_PATH"
grep -Eq "^CAP:DENY:[0-9]+:ipc_recv:-$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
