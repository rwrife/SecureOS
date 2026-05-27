#!/usr/bin/env bash
# build/scripts/test_m5_owner_delete_cascade_allow_qemu.sh
#
# Build + run the M5-on-M1 substrate allow-path peer for
# BROKER_OP_DELETE_OWNER (issue #325, plan
# plans/2026-05-25-m5-ownership-on-m1-substrate.md slice 3).
#
# Rides on the merged M1/M2/M3/M4 substrate end-to-end plus the
# slice-001/002 cascade plumbing:
#   - broker_svc_init() + fs_svc_init() bring up the broker port and
#     a real CAP_FS_READ-gated fs port to point a delegated handle at.
#   - launcher_broker_spawn_app_with_broker_cap() (slice 2 / #303)
#     spawns a real owner PCB with the broker send handle stamped
#     LE64 into ipc_scratch[24..32).
#   - helloapp_entry_broker_owner / _approve send BROKER_OP_REQUEST
#     and BROKER_OP_APPROVE over the real ipc_send_h path.
#   - The driver drains both envelopes and uses broker_svc_approve_h
#     (slice 002) so the recipient handle is parented on the owner's
#     broker-port handle, then runs broker_svc_delete_owner with
#     PID_INVALID to assert the subtree walker (step 3) fires BEFORE
#     process_destroy (step 4).
#   - Post-cascade: cap_gate_check_handle_result is CAP_ERR_MISSING
#     and ipc_send_h on the revoked handle returns IPC_ERR_CAP_DENIED.
#
# Emits the following deterministic markers (consumed by
# build/scripts/test.sh and validate_bundle.sh):
#   TEST:PASS:m5_owner_delete_cascade_allow_qemu:subtree_revoked_before_destroy_qemu
#   TEST:PASS:m5_owner_delete_cascade_allow_qemu:delegated_caps_invalid
#   TEST:PASS:m5_owner_delete_cascade_allow_qemu:audit_cascade_recorded
#   TEST:PASS:m5_owner_delete_cascade_allow_qemu:audit_cascade_done_recorded
#   TEST:PASS:m5_owner_delete_cascade_allow_qemu

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
  "$ROOT_DIR/kernel/svc/fs_svc.c" \
  "$ROOT_DIR/kernel/user/launcher.c" \
  "$ROOT_DIR/kernel/user/helloapp.c" \
  "$ROOT_DIR/tests/harness/svc_subjects.c" \
  "$ROOT_DIR/tests/harness/session_manager_stub.c" \
  "$ROOT_DIR/tests/m5_owner_delete_cascade_allow_qemu_test.c" \
  -o "$OUT_DIR/m5_owner_delete_cascade_allow_qemu_test"

LOG_PATH="$OUT_DIR/m5_owner_delete_cascade_allow_qemu_test.log"
"$OUT_DIR/m5_owner_delete_cascade_allow_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:m5_owner_delete_cascade_allow_qemu:subtree_revoked_before_destroy_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_allow_qemu:delegated_caps_invalid" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_allow_qemu:audit_cascade_recorded" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_allow_qemu:audit_cascade_done_recorded" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_allow_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
