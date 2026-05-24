#!/usr/bin/env bash
# build/scripts/test_m2_helloapp_deny_qemu.sh
#
# Build + run the M2-on-M1 substrate HelloApp deny-path validator
# (issue #270, plan plans/2026-05-23-m2-on-m1-substrate.md slice 3).
#
# Covers:
#   - A manifest with NO auto-grant still spawns a live PCB but leaves
#     ipc_scratch zeroed (slice 2 contract).
#   - helloapp_run_once() therefore decodes cap_handle_t == 0 and
#     ipc_send_h() takes the canonical handle-deny path:
#       * returns IPC_ERR_CAP_DENIED
#       * emits exactly one CAP:DENY:<subj>:console_write:- marker
#         line via the canonical cap_deny_marker formatter (#221/#244)
#       * does NOT stage an envelope into the console-svc port
#
# The deny-marker shape is validated through cap_deny_marker_validate()
# rather than substring-matching to keep the test honest about the
# single source of truth in kernel/cap/cap_deny_marker.{c,h}.
#
# Emits the following deterministic markers:
#   TEST:PASS:helloapp_denied_console_write
#   TEST:PASS:helloapp_deny

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
  "$ROOT_DIR/tests/m2_helloapp_deny_qemu_test.c" \
  -o "$OUT_DIR/m2_helloapp_deny_qemu_test"

LOG_PATH="$OUT_DIR/m2_helloapp_deny_qemu_test.log"
"$OUT_DIR/m2_helloapp_deny_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:helloapp_denied_console_write" "$LOG_PATH"
grep -q "TEST:PASS:helloapp_deny$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
