#!/usr/bin/env bash
# build/scripts/test_m2_helloapp_allow_qemu.sh
#
# Build + run the M2-on-M1 substrate HelloApp allow-path validator
# (issue #270, plan plans/2026-05-23-m2-on-m1-substrate.md slice 3).
#
# Covers:
#   - console_svc_init() allocates the well-known port (slice 1).
#   - launcher_spawn_app_from_manifest() with an auto-grant of
#     CAP_CONSOLE_WRITE produces a live PCB whose ipc_scratch carries
#     the minted cap_handle_t (slice 2).
#   - helloapp_run_once() reads the handle, builds the canonical
#     envelope, calls ipc_send_h(), and returns IPC_OK.
#   - The staged envelope drains via ipc_recv_h() with
#     sender_subject == SUBJECT_M2_HELLOAPP and payload == "helloapp\n".
#
# Emits the following deterministic markers (consumed by
# build/scripts/test.sh and validate_bundle.sh):
#   TEST:PASS:helloapp_allowed_console_write
#   TEST:PASS:helloapp_allow

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
  "$ROOT_DIR/kernel/svc/console_svc.c" \
  "$ROOT_DIR/kernel/user/launcher.c" \
  "$ROOT_DIR/kernel/user/helloapp.c" \
  "$ROOT_DIR/tests/harness/m2_subjects.c" \
  "$ROOT_DIR/tests/m2_helloapp_allow_qemu_test.c" \
  -o "$OUT_DIR/m2_helloapp_allow_qemu_test"

LOG_PATH="$OUT_DIR/m2_helloapp_allow_qemu_test.log"
"$OUT_DIR/m2_helloapp_allow_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:helloapp_allowed_console_write" "$LOG_PATH"
grep -q "TEST:PASS:helloapp_allow$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
