#!/usr/bin/env bash
# build/scripts/test_broker_svc_cascade_revokes_minted_handle.sh
#
# Build + run the M5-SUBSTRATE-002 cascade-revoke validator
# (issue #324, plan plans/2026-05-25-m5-ownership-on-m1-substrate.md
# slice 2).
#
# Covers:
#   - broker_svc_approve_h mints a recipient handle parented on the
#     owner's broker-port handle.
#   - Pre-cascade: recipient + owner handles pass cap_gate_check_handle.
#   - broker_svc_delete_owner runs the six-step cascade and returns OK.
#   - Post-cascade: both recipient + owner handles fail with
#     CAP_ERR_MISSING (slice-001 walker bumped generations).
#   - Cascade count surfaced via out_n == 1.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh and validate_bundle.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_broker.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/kernel/cap/cap_gate.c" \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/ipc/ipc_port.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/kernel/svc/broker_svc.c" \
  "$ROOT_DIR/tests/harness/svc_subjects.c" \
  "$ROOT_DIR/tests/harness/session_manager_stub.c" \
  "$ROOT_DIR/tests/broker_svc_cascade_revokes_minted_handle_test.c" \
  -o "$OUT_DIR/broker_svc_cascade_revokes_minted_handle_test"

LOG_PATH="$OUT_DIR/broker_svc_cascade_revokes_minted_handle_test.log"
"$OUT_DIR/broker_svc_cascade_revokes_minted_handle_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:broker_svc_cascade_pre_revoke_recipient_gate"  "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_cascade_pre_revoke_owner_gate"      "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_cascade_delete_owner_ok"            "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_cascade_post_revoke_recipient_gate" "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_cascade_post_revoke_owner_gate"     "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_cascade_count"                      "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_cascade_revokes_minted_handle$"     "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
