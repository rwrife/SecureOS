#!/usr/bin/env bash
# Build + run the capability audit-log slice tests.
#
# Covers:
#   - Phase 1: stable capability_audit log line shape
#   - Phase 2: grant + deny capture
#   - Phase 4: non-interference (formatting must not alter policy)
#
# Outputs deterministic TEST:PASS markers consumed by build/scripts/test.sh and
# validate_bundle.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/tests/capability_audit_log_test.c" \
  -o "$OUT_DIR/capability_audit_log_test"

LOG_PATH="$OUT_DIR/capability_audit_log_test.log"
"$OUT_DIR/capability_audit_log_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:capability_audit_log_grant_and_deny" "$LOG_PATH"
grep -q "TEST:PASS:capability_audit_log_invalid_inputs" "$LOG_PATH"
grep -q "TEST:PASS:capability_audit_log_non_interference" "$LOG_PATH"
grep -q "TEST:PASS:capability_audit_log" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
