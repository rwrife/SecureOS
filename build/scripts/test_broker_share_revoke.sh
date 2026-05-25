#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_broker.c" \
  "$ROOT_DIR/tests/broker_share_revoke_test.c" \
  -o "$OUT_DIR/broker_share_revoke_test"

LOG_PATH="$OUT_DIR/broker_share_revoke_test.log"
"$OUT_DIR/broker_share_revoke_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:broker_share_revoke:setup_grants_recipient" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_revoke:owner_revoke_takes_effect" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_revoke:underlying_table_revoked" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_revoke:double_revoke_is_idempotent" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_revoke:recipient_self_revoke" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_revoke:audit_revoke_recorded" "$LOG_PATH"
grep -q "TEST:DONE:broker_share_revoke" "$LOG_PATH"
