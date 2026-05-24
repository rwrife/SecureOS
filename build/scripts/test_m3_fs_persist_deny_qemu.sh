#!/usr/bin/env bash
# build/scripts/test_m3_fs_persist_deny_qemu.sh
#
# Build + run the M3-on-M1 substrate deny-path peer of
# fs_service_persist_deny_test.c (issue #280, plan
# plans/2026-05-24-m3-fs-on-m1-substrate.md slice 3).
#
# Covers:
#   - launcher_fs_spawn_app_with_fs_caps(..., grant_write=false) leaves
#     the ipc_scratch write slot CAP_HANDLE_NULL.
#   - helloapp_entry_fs_demo() reads the null handle, calls
#     ipc_send_h(write_handle, fs_write_port, ...) and returns
#     IPC_ERR_CAP_DENIED.
#   - Exactly one canonical CAP:DENY:<actor>:fs_write:- marker is
#     emitted (validated through cap_deny_marker_validate, #221/#265).
#   - The write port slot is still empty afterwards (no envelope
#     was staged).
#
# Emits the following deterministic markers (consumed by
# build/scripts/test.sh and validate_bundle.sh):
#   TEST:PASS:m3_fs_persist_deny_qemu_no_stage
#   TEST:PASS:m3_fs_persist_deny_marker_qemu
#   TEST:PASS:m3_fs_persist_deny_qemu

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
  "$ROOT_DIR/kernel/ipc/ipc_port.c" \
  "$ROOT_DIR/kernel/ipc/ipc_ops.c" \
  "$ROOT_DIR/kernel/proc/address_space.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/kernel/proc/proc_sched.c" \
  "$ROOT_DIR/kernel/svc/fs_svc.c" \
  "$ROOT_DIR/kernel/user/launcher.c" \
  "$ROOT_DIR/kernel/user/launcher_fs.c" \
  "$ROOT_DIR/kernel/user/helloapp.c" \
  "$ROOT_DIR/tests/harness/svc_subjects.c" \
  "$ROOT_DIR/tests/m3_fs_persist_deny_qemu_test.c" \
  -o "$OUT_DIR/m3_fs_persist_deny_qemu_test"

LOG_PATH="$OUT_DIR/m3_fs_persist_deny_qemu_test.log"
"$OUT_DIR/m3_fs_persist_deny_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:m3_fs_persist_deny_qemu_no_stage" "$LOG_PATH"
grep -q "TEST:PASS:m3_fs_persist_deny_marker_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m3_fs_persist_deny_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
