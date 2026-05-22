#!/usr/bin/env bash
# Build + run the M1 IPC port lifecycle round-trip test (issue #223).
#
# Covers:
#   - create → destroy → create on a reused slot index advances the
#     handle's generation field (no stale-handle aliasing).
#   - Every read accessor rejects a stale handle with IPC_ERR_INVALID_PORT,
#     and double-destroy is also IPC_ERR_INVALID_PORT.
#   - ipc_port_destroy(IPC_PORT_INVALID) → IPC_ERR_INVALID_PORT (no
#     underflow / no-op crash).
#   - 65535 create/destroy cycles on the same slot index never produce
#     a handle == IPC_PORT_INVALID and never let the slot index drift
#     into the generation half (wrap-around guard holds).
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
  "$ROOT_DIR/tests/ipc_port_lifecycle_test.c" \
  -o "$OUT_DIR/ipc_port_lifecycle_test"

LOG_PATH="$OUT_DIR/ipc_port_lifecycle_test.log"
"$OUT_DIR/ipc_port_lifecycle_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:ipc_port_lifecycle_handle_advances" "$LOG_PATH"
grep -q "TEST:PASS:ipc_port_lifecycle_stale_rejected" "$LOG_PATH"
grep -q "TEST:PASS:ipc_port_lifecycle_destroy_invalid" "$LOG_PATH"
grep -q "TEST:PASS:ipc_port_lifecycle_wrap_around" "$LOG_PATH"
grep -q "TEST:PASS:ipc_port_lifecycle$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
