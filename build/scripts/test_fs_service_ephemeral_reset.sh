#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/user/launcher_fs.c" \
  "$ROOT_DIR/tests/fs_service_ephemeral_reset_test.c" \
  -o "$OUT_DIR/fs_service_ephemeral_reset_test"

LOG_PATH="$OUT_DIR/fs_service_ephemeral_reset_test.log"
"$OUT_DIR/fs_service_ephemeral_reset_test" | tee "$LOG_PATH"

grep -q "TEST:START:fs_service_ephemeral_reset" "$LOG_PATH"
grep -q "TEST:PASS:fs_service_ephemeral_reset:write_to_faux_succeeds" "$LOG_PATH"
grep -q "TEST:PASS:fs_service_ephemeral_reset:visible_in_same_session" "$LOG_PATH"
grep -q "TEST:PASS:fs_service_ephemeral_reset:gone_after_relaunch" "$LOG_PATH"
grep -q "TEST:PASS:fs_service_ephemeral_reset:no_persist_leak" "$LOG_PATH"
grep -q "TEST:PASS:fs_service_ephemeral_reset$" "$LOG_PATH"
