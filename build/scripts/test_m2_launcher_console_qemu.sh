#!/usr/bin/env bash
# build/scripts/test_m2_launcher_console_qemu.sh
#
# Build + run the M2-on-M1 substrate launcher_console `_qemu` peer
# (issue #271, plan plans/2026-05-23-m2-on-m1-substrate.md slice 4).
#
# Covers all four BUILD_ROADMAP §5.2 launcher-mediation markers
# driven through the real substrate (spawn -> handoff -> IPC -> revoke):
#
#   TEST:PASS:launcher_console_deny_without_grant
#   TEST:PASS:launcher_console_allow_after_grant
#   TEST:PASS:launcher_console_regression_bypass_denied
#   TEST:PASS:launcher_console_revoke_restores_deny
#
# Every deny marker is shape-validated via cap_deny_marker_validate()
# against the §4 grammar so the test never silently accepts a
# shape-shifted line (single source of truth: kernel/cap/cap_deny_marker).

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
  "$ROOT_DIR/tests/m2_launcher_console_qemu_test.c" \
  -o "$OUT_DIR/m2_launcher_console_qemu_test"

LOG_PATH="$OUT_DIR/m2_launcher_console_qemu_test.log"
"$OUT_DIR/m2_launcher_console_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:launcher_console_deny_without_grant" "$LOG_PATH"
grep -q "TEST:PASS:launcher_console_allow_after_grant" "$LOG_PATH"
grep -q "TEST:PASS:launcher_console_regression_bypass_denied" "$LOG_PATH"
grep -q "TEST:PASS:launcher_console_revoke_restores_deny" "$LOG_PATH"
grep -q "TEST:PASS:m2_launcher_console_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
