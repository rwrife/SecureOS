#!/usr/bin/env bash
# build/scripts/test_launcher_broker_spawn_handoff.sh
#
# Build + run the M4-on-M1 substrate launcher broker-spawn handoff
# validator (issue #303, plan plans/2026-05-25-m4-broker-on-m1-substrate.md
# slice 2).
#
# Covers:
#   - launcher_broker_spawn_app_with_broker_cap(...) spawns a real
#     PCB, mints CAP_IPC_SEND for the broker-svc port, and writes
#     the handle LE64 into ipc_scratch[24..32). The lower (console,
#     fs) slots and the upper reserved bytes stay zero.
#   - The minted handle gate-checks positively for CAP_IPC_SEND and
#     negatively for unrelated caps (CAP_CONSOLE_WRITE, CAP_FS_READ).
#   - launcher_broker_spawn_destroy() cascades
#     cap_handle_revoke_subject() so the broker handle fails closed
#     afterwards.
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
  "$ROOT_DIR/kernel/cap/cap_gate.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/kernel/proc/address_space.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/kernel/user/launcher.c" \
  "$ROOT_DIR/tests/harness/svc_subjects.c" \
  "$ROOT_DIR/tests/launcher_broker_spawn_handoff_test.c" \
  -o "$OUT_DIR/launcher_broker_spawn_handoff_test"

LOG_PATH="$OUT_DIR/launcher_broker_spawn_handoff_test.log"
"$OUT_DIR/launcher_broker_spawn_handoff_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:launcher_broker_spawn_handoff_stamp" "$LOG_PATH"
grep -q "TEST:PASS:launcher_broker_spawn_handoff_gate" "$LOG_PATH"
grep -q "TEST:PASS:launcher_broker_spawn_handoff_revoke_on_destroy" "$LOG_PATH"
grep -q "TEST:PASS:launcher_broker_spawn_handoff$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
