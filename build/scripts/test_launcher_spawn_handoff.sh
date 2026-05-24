#!/usr/bin/env bash
# build/scripts/test_launcher_spawn_handoff.sh
#
# Build + run the M2-on-M1 substrate launcher-spawn handoff validator
# (issue #269, plan plans/2026-05-23-m2-on-m1-substrate.md slice 2).
#
# Covers:
#   - launcher_spawn_app_from_manifest() carves a fresh address_space_t
#     window, spawns a real PCB via process_create, mints a
#     cap_handle_t, and writes it little-endian into the first 4 bytes
#     of the spawned aspace's ipc_scratch region.
#   - The minted handle round-trips through cap_gate_check_handle().
#   - A manifest with no auto-grant leaves the scratch slot zeroed.
#   - launcher_spawn_destroy() revokes the handle.
#   - Invalid manifests (NULL, subject_id == 0, unsupported cap,
#     NULL caps pointer with non-zero count) are rejected with
#     LAUNCHER_ERR_INVALID_MANIFEST.
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
  "$ROOT_DIR/kernel/proc/address_space.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/kernel/user/launcher.c" \
  "$ROOT_DIR/tests/harness/m2_subjects.c" \
  "$ROOT_DIR/tests/launcher_spawn_handoff_test.c" \
  -o "$OUT_DIR/launcher_spawn_handoff_test"

LOG_PATH="$OUT_DIR/launcher_spawn_handoff_test.log"
"$OUT_DIR/launcher_spawn_handoff_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:launcher_spawn_handoff_grant" "$LOG_PATH"
grep -q "TEST:PASS:launcher_spawn_handoff_no_grant" "$LOG_PATH"
grep -q "TEST:PASS:launcher_spawn_handoff_destroy" "$LOG_PATH"
grep -q "TEST:PASS:launcher_spawn_handoff_invalid_manifest" "$LOG_PATH"
grep -q "TEST:PASS:launcher_spawn_handoff$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
