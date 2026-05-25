#!/usr/bin/env bash
# build/scripts/test_broker_svc_port_alloc.sh
#
# Build + run the M4-on-M1 substrate broker-service port-allocation
# validator (issue #302, plan plans/2026-05-25-m4-broker-on-m1-substrate.md
# slice 1).
#
# Covers:
#   - Uninitialised state: is_initialised=false, port=IPC_PORT_INVALID.
#   - Post-init state: handle valid; owner=SUBJECT_M4_BROKER_SVC;
#     send_cap=recv_cap=CAP_IPC_SEND (option 1 — broker authority is
#     subject-bound, not cap-bound).
#   - Double init returns BROKER_SVC_ERR_ALREADY_INIT and does not
#     allocate a fresh port.
#   - Reset clears state; subsequent init succeeds.
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
  "$ROOT_DIR/kernel/ipc/ipc_port.c" \
  "$ROOT_DIR/kernel/svc/broker_svc.c" \
  "$ROOT_DIR/tests/harness/svc_subjects.c" \
  "$ROOT_DIR/tests/broker_svc_port_alloc_test.c" \
  -o "$OUT_DIR/broker_svc_port_alloc_test"

LOG_PATH="$OUT_DIR/broker_svc_port_alloc_test.log"
"$OUT_DIR/broker_svc_port_alloc_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:broker_svc_port_alloc_uninit" "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_port_alloc_init" "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_port_alloc_double_init" "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_port_alloc_reset" "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_port_alloc$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
