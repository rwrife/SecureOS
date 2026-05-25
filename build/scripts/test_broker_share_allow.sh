#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_broker.c" \
  "$ROOT_DIR/tests/broker_share_allow_test.c" \
  -o "$OUT_DIR/broker_share_allow_test"

LOG_PATH="$OUT_DIR/broker_share_allow_test.log"
"$OUT_DIR/broker_share_allow_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:broker_share_allow:owner_holds_cap" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_allow:request_returns_pending_share_id" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_allow:approve_grants_recipient" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_allow:scope_is_resource_bound" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_allow:scope_is_capability_bound" "$LOG_PATH"
grep -q "TEST:PASS:broker_share_allow:audit_grant_recorded" "$LOG_PATH"
grep -q "TEST:DONE:broker_share_allow" "$LOG_PATH"
