#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/user/launcher_fs.c" \
  "$ROOT_DIR/tests/launcher_fs_test.c" \
  -o "$OUT_DIR/launcher_fs_test"

LOG_PATH="$OUT_DIR/launcher_fs_test.log"
"$OUT_DIR/launcher_fs_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:launcher_fs_deny_without_grant" "$LOG_PATH"
grep -q "TEST:PASS:launcher_fs_allow_after_grant" "$LOG_PATH"
grep -q "TEST:PASS:launcher_fs_revoke_restores_deny" "$LOG_PATH"
grep -q "TEST:PASS:launcher_fs_persistent_survives_relaunch" "$LOG_PATH"
grep -q "TEST:PASS:launcher_fs_ephemeral_resets_on_relaunch" "$LOG_PATH"
grep -q "TEST:PASS:launcher_fs_cross_app_isolation" "$LOG_PATH"
grep -q "TEST:PASS:launcher_fs_bypass_unregistered_denied" "$LOG_PATH"
grep -q "TEST:PASS:launcher_fs_invalid_inputs" "$LOG_PATH"
grep -q "TEST:PASS:launcher_fs$" "$LOG_PATH"
