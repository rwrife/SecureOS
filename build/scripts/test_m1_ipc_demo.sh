#!/usr/bin/env bash
# Build + run the M1 two-module IPC acceptance demo (issue #251,
# plan plans/2026-05-20-m1-process-address-space.md slice 4,
# BUILD_ROADMAP §5.1).
#
# Covers:
#   - allow path: m1-sender -> m1-receiver round-trip via ipc_send_h /
#     ipc_recv_h, kernel-stamped sender_subject preserved.
#   - deny path: m1-unauth ipc_send_h with a wrong-cap handle returns
#     IPC_ERR_CAP_DENIED, exactly one canonical CAP:DENY marker is
#     emitted, receiver remains BLOCKED, no envelope leaks.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh and validate_bundle.sh:
#   TEST:PASS:m1_ipc_allow
#   TEST:PASS:m1_ipc_deny
#   TEST:PASS:m1_ipc

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/kernel/proc/proc_sched.c" \
  "$ROOT_DIR/kernel/proc/module_registry.c" \
  "$ROOT_DIR/kernel/ipc/ipc_port.c" \
  "$ROOT_DIR/kernel/ipc/ipc_ops.c" \
  "$ROOT_DIR/tests/m1_ipc_demo_test.c" \
  -o "$OUT_DIR/m1_ipc_demo_test"

LOG_PATH="$OUT_DIR/m1_ipc_demo_test.log"
"$OUT_DIR/m1_ipc_demo_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:m1_ipc_allow" "$LOG_PATH"
grep -q "TEST:PASS:m1_ipc_deny"  "$LOG_PATH"
grep -q "TEST:PASS:m1_ipc$"      "$LOG_PATH"
# Canonical deny marker per docs/abi/capability-deny-contract.md §4.
# SUBJECT_M1_UNAUTH = 7 (see kernel/proc/module_registry.h).
grep -Eq "^CAP:DENY:7:ipc_send:-$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
