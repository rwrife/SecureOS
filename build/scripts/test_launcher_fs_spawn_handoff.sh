#!/usr/bin/env bash
# build/scripts/test_launcher_fs_spawn_handoff.sh
#
# Build + run the M3-on-M1 substrate launcher fs-spawn handoff
# validator (issue #279, plan plans/2026-05-24-m3-fs-on-m1-substrate.md
# slice 2).
#
# Covers:
#   - launcher_fs_spawn_app_with_fs_caps(..., grant_write=false, ...)
#     spawns a real PCB, mints CAP_FS_READ, and writes it LE64 into
#     ipc_scratch[8..16). ipc_scratch[16..24) stays CAP_HANDLE_NULL.
#   - launcher_fs_spawn_app_with_fs_caps(..., grant_write=true, ...)
#     additionally mints CAP_FS_WRITE into ipc_scratch[16..24); both
#     handles gate-check and resolve to the spawned subject.
#   - launcher_fs_spawn_destroy() cascades cap_handle_revoke_subject()
#     so both fs handles fail closed afterwards.
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
  "$ROOT_DIR/kernel/user/launcher_fs.c" \
  "$ROOT_DIR/tests/harness/svc_subjects.c" \
  "$ROOT_DIR/tests/launcher_fs_spawn_handoff_test.c" \
  -o "$OUT_DIR/launcher_fs_spawn_handoff_test"

LOG_PATH="$OUT_DIR/launcher_fs_spawn_handoff_test.log"
"$OUT_DIR/launcher_fs_spawn_handoff_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:launcher_fs_spawn_handoff_read_only" "$LOG_PATH"
grep -q "TEST:PASS:launcher_fs_spawn_handoff_read_write" "$LOG_PATH"
grep -q "TEST:PASS:launcher_fs_spawn_handoff_revoke_on_destroy" "$LOG_PATH"
grep -q "TEST:PASS:launcher_fs_spawn_handoff$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
