#!/usr/bin/env bash
# build/scripts/test_console_svc_port_alloc.sh
#
# Build + run the M2-on-M1 substrate console-service port-allocation
# validator (issue #268, plan plans/2026-05-23-m2-on-m1-substrate.md
# slice 1).
#
# Covers:
#   - Uninitialised state: is_initialised=false, port=IPC_PORT_INVALID.
#   - Post-init state: handle valid, owner=SUBJECT_M2_CONSOLE_SVC,
#     send_cap=recv_cap=CAP_CONSOLE_WRITE.
#   - Double init returns CONSOLE_SVC_ERR_ALREADY_INIT and does not
#     allocate a second port.
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
  "$ROOT_DIR/kernel/svc/console_svc.c" \
  "$ROOT_DIR/tests/harness/m2_subjects.c" \
  "$ROOT_DIR/tests/console_svc_port_alloc_test.c" \
  -o "$OUT_DIR/console_svc_port_alloc_test"

LOG_PATH="$OUT_DIR/console_svc_port_alloc_test.log"
"$OUT_DIR/console_svc_port_alloc_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:console_svc_port_alloc_uninit" "$LOG_PATH"
grep -q "TEST:PASS:console_svc_port_alloc_init" "$LOG_PATH"
grep -q "TEST:PASS:console_svc_port_alloc_double_init" "$LOG_PATH"
grep -q "TEST:PASS:console_svc_port_alloc_reset" "$LOG_PATH"
grep -q "TEST:PASS:console_svc_port_alloc$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
