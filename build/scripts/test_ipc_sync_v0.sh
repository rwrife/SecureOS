#!/usr/bin/env bash
# Build + run the M1 synchronous IPC primitive v0 tests (issue #210).
#
# Covers:
#   - allow path (two modules exchange one envelope)
#   - deny path (CAP:DENY:<subject>:ipc_send:- marker present + cap audit
#     ring records the deny event)
#   - ipc_call round-trip
#   - envelope ABI invariants (size, field offsets via static_assert,
#     abi_version / flags / payload_len rejection)
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
  "$ROOT_DIR/tests/ipc_sync_v0_test.c" \
  -o "$OUT_DIR/ipc_sync_v0_test"

LOG_PATH="$OUT_DIR/ipc_sync_v0_test.log"
"$OUT_DIR/ipc_sync_v0_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:ipc_sync_v0_abi_envelope" "$LOG_PATH"
grep -q "TEST:PASS:ipc_sync_v0_allow_send_recv" "$LOG_PATH"
grep -q "TEST:PASS:ipc_sync_v0_deny_marker" "$LOG_PATH"
grep -q "TEST:PASS:ipc_sync_v0_call_round_trip" "$LOG_PATH"
grep -q "TEST:PASS:ipc_sync_v0" "$LOG_PATH"
# Capability-deny contract marker (docs/abi/capability-deny-contract.md §4):
# CAP:DENY:<subject>:ipc_send:- emitted by ipc_ops.c on the deny path.
grep -Eq "^CAP:DENY:[0-9]+:ipc_send:-$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
