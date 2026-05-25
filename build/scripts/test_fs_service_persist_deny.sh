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
# Audit-deny assertion is wired via issue #311 (launcher_fs persistent-write
# deny now publishes into the cap_audit ring through cap_audit_emit()).
grep -q "TEST:PASS:fs_service_persist_deny:audit_deny_recorded" "$LOG_PATH"
grep -q "TEST:PASS:fs_service_persist_deny$" "$LOG_PATH"
