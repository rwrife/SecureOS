#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_broker.c" \
  "$ROOT_DIR/tests/broker_share_deny_test.c" \
  -o "$OUT_DIR/broker_share_deny_test"

LOG_PATH="$OUT_DIR/broker_share_deny_test.log"
"$OUT_DIR/broker_share_deny_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:broker_share_deny:owner_holds_cap" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_deny:request_returns_pending_share_id" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_deny:deny_path" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_deny:no_recipient_grant" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_deny:cannot_be_re_approved" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_deny:bystander_cannot_mutate" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_deny:audit_deny_recorded" "$LOG_PATH"
grep -q "TEST:DONE:broker_share_deny" "$LOG_PATH"
