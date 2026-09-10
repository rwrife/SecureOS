#!/usr/bin/env bash
# Build + run malformed-envelope IPC host fixture (issue #586).
#
# Pins docs/abi/ipc-wire.md malformed-message branches for v0:
#   - NULL outbound envelope pointer
#   - abi_version mismatch
#   - reserved flags (MBZ) violation
#   - payload_len overflow
#   - delivered sender_subject == 0 rejection
#
# Also asserts each rejection leaves the rendezvous slot usable by a
# subsequent valid send/recv round-trip.

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
  "$ROOT_DIR/tests/ipc_wire_malformed_test.c" \
  -o "$OUT_DIR/ipc_wire_malformed_test"

LOG_PATH="$OUT_DIR/ipc_wire_malformed_test.log"
"$OUT_DIR/ipc_wire_malformed_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:ipc_wire_malformed:null_message_pointer_returns_invalid_msg" "$LOG_PATH"
grep -q "TEST:PASS:ipc_wire_malformed:abi_version_mismatch_returns_invalid_msg" "$LOG_PATH"
grep -q "TEST:PASS:ipc_wire_malformed:reserved_flags_returns_invalid_msg" "$LOG_PATH"
grep -q "TEST:PASS:ipc_wire_malformed:oversized_payload_returns_invalid_msg" "$LOG_PATH"
grep -q "TEST:PASS:ipc_wire_malformed:sender_subject_zero_on_delivery_returns_invalid_msg" "$LOG_PATH"
grep -q "TEST:PASS:ipc_wire_malformed$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
