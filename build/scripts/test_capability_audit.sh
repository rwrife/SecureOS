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
