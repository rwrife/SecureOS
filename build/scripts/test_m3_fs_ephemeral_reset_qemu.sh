#!/usr/bin/env bash
# build/scripts/test_m3_fs_ephemeral_reset_qemu.sh
#
# Build + run the M3-on-M1 substrate ephemeral-reset peer of
# fs_service_ephemeral_reset_test.c (issue #281, plan
# plans/2026-05-24-m3-fs-on-m1-substrate.md slice 4).
#
# Rides on the merged M1 substrate end-to-end (no QEMU image; the
# `_qemu` suffix denotes "rides on the real M1 substrate" per the
# convention #259/#270/#280 already committed to):
#   - fs_svc_init() allocates the two well-known fs ports (slice 1).
#   - launcher_fs_register_app(..., LAUNCHER_FS_MODE_EPHEMERAL) sets
#     up the launcher-mediated persistence boundary.
#   - launcher_fs_spawn_app_with_fs_caps(..., grant_write=1) spawns a
#     real PCB and stamps both fs handles into ipc_scratch (slice 2).
#   - helloapp_entry_fs_demo() reads the handles and issues one
#     ipc_send_h per leg (slice 3 module, #280).
#   - process_destroy + fresh launcher_fs_spawn_app_with_fs_caps
#     proves the substrate-level handle revocation half; the
#     launcher-policy hook (launcher_fs_app_relaunch) drives the
#     faux-store wipe half. NOT used as a substitute for the real
#     process recycle.
#   - A SECOND, persistent peer at the same path after the recycle
#     confirms the ephemeral blob never spilled upward into the
#     persistent ramfs.
#
# Emits the following deterministic markers (consumed by
# build/scripts/test.sh and validate_bundle.sh):
#   TEST:PASS:m3_fs_ephemeral_reset_qemu:write_to_faux_succeeds
#   TEST:PASS:m3_fs_ephemeral_reset_qemu:visible_in_same_session
#   TEST:PASS:m3_fs_ephemeral_reset_qemu:gone_after_relaunch
#   TEST:PASS:m3_fs_ephemeral_reset_qemu:no_persist_leak
#   TEST:PASS:m3_fs_ephemeral_reset_qemu

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
  "$ROOT_DIR/tests/m3_fs_ephemeral_reset_qemu_test.c" \
  -o "$OUT_DIR/m3_fs_ephemeral_reset_qemu_test"

LOG_PATH="$OUT_DIR/m3_fs_ephemeral_reset_qemu_test.log"
"$OUT_DIR/m3_fs_ephemeral_reset_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:m3_fs_ephemeral_reset_qemu:write_to_faux_succeeds" "$LOG_PATH"
grep -q "TEST:PASS:m3_fs_ephemeral_reset_qemu:visible_in_same_session" "$LOG_PATH"
grep -q "TEST:PASS:m3_fs_ephemeral_reset_qemu:gone_after_relaunch" "$LOG_PATH"
grep -q "TEST:PASS:m3_fs_ephemeral_reset_qemu:no_persist_leak" "$LOG_PATH"
grep -q "TEST:PASS:m3_fs_ephemeral_reset_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
