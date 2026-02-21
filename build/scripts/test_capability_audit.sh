#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/tests/capability_audit_test.c" \
  -o "$OUT_DIR/capability_audit_test"

LOG_PATH="$OUT_DIR/capability_audit_test.log"
"$OUT_DIR/capability_audit_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:capability_audit_core_paths" "$LOG_PATH"
grep -q "TEST:PASS:capability_audit_ring_wrap" "$LOG_PATH"
grep -q "TEST:PASS:capability_audit_mixed_overflow" "$LOG_PATH"

SUMMARY_LINE="$(grep 'TEST:AUDIT_SUMMARY:' "$LOG_PATH" | tail -n 1)"
COUNT="$(echo "$SUMMARY_LINE" | sed -n 's/.*count=\([0-9][0-9]*\).*/\1/p')"
DROPPED="$(echo "$SUMMARY_LINE" | sed -n 's/.*dropped=\([0-9][0-9]*\).*/\1/p')"

if [[ -z "$COUNT" || -z "$DROPPED" ]]; then
  echo "Missing audit summary markers in capability audit output" >&2
  exit 1
fi

cat > "$OUT_DIR/capability_audit_summary.json" <<JSON
{
  "schemaVersion": 1,
  "test": "capability_audit",
  "ringCapacity": ${CAP_AUDIT_EVENT_MAX:-32},
  "retainedEvents": $COUNT,
  "droppedEvents": $DROPPED
}
JSON
