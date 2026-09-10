#!/usr/bin/env bash
# build/scripts/test_m5_ownership_role_manifest_cascade_qemu.sh
#
# Build + run issue #585 substrate peer that verifies launcher-root cascade
# semantics for ownership_role broker wiring:
#   - owner role: launcher-root delete cascades through owner broker handle
#   - delegate role: broker-minted delegated handles stale under same cascade

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
  "$ROOT_DIR/tests/m5_ownership_role_manifest_cascade_qemu_test.c" \
  -o "$OUT_DIR/m5_ownership_role_manifest_cascade_qemu_test"

LOG_PATH="$OUT_DIR/m5_ownership_role_manifest_cascade_qemu_test.log"
"$OUT_DIR/m5_ownership_role_manifest_cascade_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:m5_ownership_role_owner_cascade_qemu$" "$LOG_PATH"
grep -q "TEST:PASS:m5_ownership_role_delegate_caps_invalid_qemu$" "$LOG_PATH"
grep -q "TEST:PASS:m5_ownership_role_manifest_cascade_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
