#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_broker.c" \
  "$ROOT_DIR/tests/cap_broker_test.c" \
  -o "$OUT_DIR/cap_broker_test"

LOG_PATH="$OUT_DIR/cap_broker_test.log"
"$OUT_DIR/cap_broker_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:cap_broker_owner_must_hold_cap" "$LOG_PATH"
grep -q "TEST:PASS:cap_broker_request_input_validation" "$LOG_PATH"
grep -q "TEST:PASS:cap_broker_deny_path" "$LOG_PATH"
grep -q "TEST:PASS:cap_broker_allow_path" "$LOG_PATH"
grep -q "TEST:PASS:cap_broker_revoke_path" "$LOG_PATH"
grep -q "TEST:PASS:cap_broker_recipient_self_revoke" "$LOG_PATH"
grep -q "TEST:PASS:cap_broker_invalid_ids" "$LOG_PATH"
grep -q "TEST:PASS:cap_broker_reset_clears_state" "$LOG_PATH"
grep -q "TEST:DONE:cap_broker" "$LOG_PATH"
