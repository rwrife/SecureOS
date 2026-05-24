#!/usr/bin/env bash
# build/scripts/test_fs_svc_port_alloc.sh
#
# Build + run the M3-on-M1 substrate fs-service port-allocation
# validator (issue #278, plan plans/2026-05-24-m3-fs-on-m1-substrate.md
# slice 1).
#
# Covers:
#   - Uninitialised state: is_initialised=false, both ports=IPC_PORT_INVALID.
#   - Post-init state: both handles valid + distinct; owner=SUBJECT_M3_FS_SVC;
#     read port send_cap=recv_cap=CAP_FS_READ; write port send_cap=recv_cap=CAP_FS_WRITE.
#   - Double init returns FS_SVC_ERR_ALREADY_INIT and does not allocate fresh ports.
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
  "$ROOT_DIR/kernel/svc/fs_svc.c" \
  "$ROOT_DIR/tests/harness/svc_subjects.c" \
  "$ROOT_DIR/tests/fs_svc_port_alloc_test.c" \
  -o "$OUT_DIR/fs_svc_port_alloc_test"

LOG_PATH="$OUT_DIR/fs_svc_port_alloc_test.log"
"$OUT_DIR/fs_svc_port_alloc_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:fs_svc_port_alloc_uninit" "$LOG_PATH"
grep -q "TEST:PASS:fs_svc_port_alloc_init" "$LOG_PATH"
grep -q "TEST:PASS:fs_svc_port_alloc_double_init" "$LOG_PATH"
grep -q "TEST:PASS:fs_svc_port_alloc_reset" "$LOG_PATH"
grep -q "TEST:PASS:fs_svc_port_alloc$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
