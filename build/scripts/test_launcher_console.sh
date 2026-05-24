#!/usr/bin/env bash
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
  "$ROOT_DIR/tests/launcher_console_test.c" \
  -o "$OUT_DIR/launcher_console_test"

LOG_PATH="$OUT_DIR/launcher_console_test.log"
"$OUT_DIR/launcher_console_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:launcher_console_deny_without_grant" "$LOG_PATH"
grep -q "TEST:PASS:launcher_console_allow_after_grant" "$LOG_PATH"
grep -q "TEST:PASS:launcher_console_regression_bypass_denied" "$LOG_PATH"
grep -q "TEST:PASS:launcher_console_revoke_restores_deny" "$LOG_PATH"
grep -q "TEST:PASS:launcher_console_invalid_app" "$LOG_PATH"
grep -q "TEST:PASS:launcher_console_reset_clears_state" "$LOG_PATH"
