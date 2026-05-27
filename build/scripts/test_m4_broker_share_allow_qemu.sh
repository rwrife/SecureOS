#!/usr/bin/env bash
# build/scripts/test_m4_broker_share_allow_qemu.sh
#
# Build + run the M4-on-M1 substrate allow-path peer of
# broker_share_allow_test.c (issue #304, plan
# plans/2026-05-25-m4-broker-on-m1-substrate.md slice 3).
#
# Rides on the merged M1 substrate end-to-end:
#   - broker_svc_init() allocates the well-known broker port (slice 1).
#   - launcher_broker_spawn_app_with_broker_cap() spawns a real PCB
#     with the CAP_IPC_SEND broker handle stamped LE64 into
#     ipc_scratch[24..32) (slice 2).
#   - helloapp_entry_broker_owner() (slice 3) reads the handle and
#     issues BROKER_OP_REQUEST + BROKER_OP_APPROVE via ipc_send_h.
#   - The test driver drains both envelopes via ipc_recv_h and fans
#     the parsed ops into cap_broker_*.
#
# Emits the following deterministic markers (consumed by
# build/scripts/test.sh and validate_bundle.sh):
#   TEST:PASS:m4_broker_owner_qemu:request  (>=2 occurrences)
#   TEST:PASS:m4_broker_owner_qemu:approve  (>=1 occurrence)
#   TEST:PASS:m4_broker_share_allow_qemu:owner_holds_cap_qemu
#   TEST:PASS:m4_broker_share_allow_qemu:request_returns_pending_share_id_qemu
#   TEST:PASS:m4_broker_share_allow_qemu:approve_grants_recipient_qemu
#   TEST:PASS:m4_broker_share_allow_qemu:scope_is_capability_bound_qemu
#   TEST:PASS:m4_broker_share_allow_qemu:scope_is_resource_bound_qemu
#   TEST:PASS:m4_broker_share_allow_qemu

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/kernel/cap/cap_gate.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/kernel/cap/cap_broker.c" \
  "$ROOT_DIR/kernel/ipc/ipc_port.c" \
  "$ROOT_DIR/kernel/ipc/ipc_ops.c" \
  "$ROOT_DIR/kernel/proc/address_space.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/kernel/proc/proc_sched.c" \
  "$ROOT_DIR/kernel/svc/broker_svc.c" \
  "$ROOT_DIR/kernel/user/launcher.c" \
  "$ROOT_DIR/kernel/user/helloapp.c" \
  "$ROOT_DIR/tests/harness/svc_subjects.c" \
  "$ROOT_DIR/tests/harness/session_manager_stub.c" \
  "$ROOT_DIR/tests/m4_broker_share_allow_qemu_test.c" \
  -o "$OUT_DIR/m4_broker_share_allow_qemu_test"

LOG_PATH="$OUT_DIR/m4_broker_share_allow_qemu_test.log"
"$OUT_DIR/m4_broker_share_allow_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:m4_broker_share_allow_qemu:owner_holds_cap_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_allow_qemu:request_returns_pending_share_id_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_allow_qemu:approve_grants_recipient_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_allow_qemu:scope_is_capability_bound_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_allow_qemu:scope_is_resource_bound_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_allow_qemu$" "$LOG_PATH"
# Helloapp broker-demo owner-side markers — one request, one approve.
req_count=$(grep -c "^TEST:PASS:m4_broker_owner_qemu:request$" "$LOG_PATH" || true)
app_count=$(grep -c "^TEST:PASS:m4_broker_owner_qemu:approve$" "$LOG_PATH" || true)
if [ "$req_count" -lt 1 ]; then
  echo "FAIL: expected >=1 m4_broker_owner_qemu:request marker, got $req_count" >&2
  exit 1
fi
if [ "$app_count" -lt 1 ]; then
  echo "FAIL: expected >=1 m4_broker_owner_qemu:approve marker, got $app_count" >&2
  exit 1
fi
! grep -q "TEST:FAIL:" "$LOG_PATH"
