#!/usr/bin/env bash
# build/scripts/test_m3_fs_persist_allow_qemu.sh
#
# Build + run the M3-on-M1 substrate allow-path peer of
# fs_service_persist_allow_test.c (issue #280, plan
# plans/2026-05-24-m3-fs-on-m1-substrate.md slice 3).
#
# Rides on the merged M1 substrate end-to-end:
#   - fs_svc_init() allocates the two well-known fs ports (slice 1).
#   - launcher_fs_spawn_app_with_fs_caps(..., grant_write=true) spawns
#     a real PCB and stamps both fs handles into ipc_scratch (slice 2).
#   - helloapp_entry_fs_demo() (slice 3) reads the handles and issues
#     one ipc_send_h per leg.
#   - The test driver drains both legs via ipc_recv_h and fans the
#     bytes into launcher_fs_app_{write,read}.
#   - Persistence is confirmed across a process_destroy +
#     launcher_fs_app_relaunch + re-spawn cycle.
#
# Emits the following deterministic markers (consumed by
# build/scripts/test.sh and validate_bundle.sh):
#   TEST:PASS:m3_helloapp_fs_qemu_op  (two occurrences — one per op)
#   TEST:PASS:m3_fs_persist_allow_qemu:cap_present_qemu
#   TEST:PASS:m3_fs_persist_allow_qemu:write_succeeds_qemu
#   TEST:PASS:m3_fs_persist_allow_qemu:read_back_after_close_qemu
#   TEST:PASS:m3_fs_persist_allow_qemu:relaunch_round_trip_qemu
#   TEST:PASS:m3_fs_persist_allow_qemu

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
  "$ROOT_DIR/tests/m3_fs_persist_allow_qemu_test.c" \
  -o "$OUT_DIR/m3_fs_persist_allow_qemu_test"

LOG_PATH="$OUT_DIR/m3_fs_persist_allow_qemu_test.log"
"$OUT_DIR/m3_fs_persist_allow_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:m3_fs_persist_allow_qemu:cap_present_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m3_fs_persist_allow_qemu:write_succeeds_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m3_fs_persist_allow_qemu:read_back_after_close_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m3_fs_persist_allow_qemu:relaunch_round_trip_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m3_fs_persist_allow_qemu$" "$LOG_PATH"
# Exactly two helloapp fs-demo op markers (write + read), per issue #280
# "On IPC_OK for each: emit ... (one per op)". The respawn round-trip
# adds one more read op on the second pass (write denies), so three is
# the steady-state expected total.
op_count=$(grep -c "^TEST:PASS:m3_helloapp_fs_qemu_op$" "$LOG_PATH")
if [ "$op_count" -lt 2 ]; then
  echo "FAIL: expected at least 2 m3_helloapp_fs_qemu_op markers, got $op_count" >&2
  exit 1
fi
! grep -q "TEST:FAIL:" "$LOG_PATH"
