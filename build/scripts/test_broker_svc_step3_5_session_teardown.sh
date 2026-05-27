#!/usr/bin/env bash
# build/scripts/test_broker_svc_step3_5_session_teardown.sh
#
# Build + run the M5-SUBSTRATE-005b step-3.5 WM-session teardown
# validator (issue #350, plan
# plans/2026-05-26-m5-wm-cascade-on-substrate.md slice 005b).
#
# Covers:
#   - Zero-session owner: step 3.5 is a no-op; *out_n unchanged.
#   - Single-session owner: exactly one session_manager_destroy;
#     *out_n incremented by 1.
#   - Three-session owner + one bystander session: all three owner
#     sessions destroyed; bystander session untouched; cascade total
#     surfaces all 3 in *out_n; post-cascade enumerator misses on
#     owner subject.
#
# Pure host. session_manager is the harness stub in
# tests/harness/session_manager_stub.{c,h}, not the kernel impl.

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
  "$ROOT_DIR/tests/broker_svc_step3_5_session_teardown_test.c" \
  -o "$OUT_DIR/broker_svc_step3_5_session_teardown_test"

LOG_PATH="$OUT_DIR/broker_svc_step3_5_session_teardown_test.log"
"$OUT_DIR/broker_svc_step3_5_session_teardown_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:broker_svc_step3_5_no_session_owner"                "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_step3_5_single_session_destroyed"        "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_step3_5_multiple_sessions_destroyed"     "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_step3_5_post_enumerator_misses"          "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_step3_5_unrelated_subject_isolated"      "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_step3_5_cascade_count_includes_sessions" "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_step3_5_session_teardown$"               "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
