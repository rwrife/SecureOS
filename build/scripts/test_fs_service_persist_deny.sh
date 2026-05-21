#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/user/launcher_fs.c" \
  "$ROOT_DIR/tests/fs_service_persist_deny_test.c" \
  -o "$OUT_DIR/fs_service_persist_deny_test"

LOG_PATH="$OUT_DIR/fs_service_persist_deny_test.log"
"$OUT_DIR/fs_service_persist_deny_test" | tee "$LOG_PATH"

grep -q "TEST:START:fs_service_persist_deny" "$LOG_PATH"
grep -q "TEST:PASS:fs_service_persist_deny:cap_absent" "$LOG_PATH"
grep -q "TEST:PASS:fs_service_persist_deny:fail_closed" "$LOG_PATH"
grep -q "TEST:PASS:fs_service_persist_deny:redirected_to_ephemeral" "$LOG_PATH"
grep -q "TEST:PASS:fs_service_persist_deny:no_persist_visibility:fail_closed" "$LOG_PATH"
grep -q "TEST:PASS:fs_service_persist_deny:no_persist_visibility:redirected" "$LOG_PATH"
# Audit-deny assertion is gated on #84 / #98 (capability audit ring). Until
# that wires into launcher_fs deny path on main, the slice emits SKIP, which
# we still assert is present so the validator JSON report distinguishes
# "not asserted" from "asserted and passed".
grep -q "TEST:SKIP:fs_service_persist_deny:audit_deny_recorded:audit_log_unwired" "$LOG_PATH"
grep -q "TEST:PASS:fs_service_persist_deny$" "$LOG_PATH"
