#!/usr/bin/env bash
# Build + run the M2 console capability-audit byte-exact fixture-diff test
# (M1-CAPTBL-005, plan plans/2026-05-20-m1-kernel-capability-table.md).
#
# This test pins the byte-stream emitted by the capability subsystem under a
# representative M2 console sequence so a future migration of cap_table.{c,h}
# onto cap_handle.{c,h} cannot silently alter the audit ABI.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_gate.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/tests/capability_audit_fixture_test.c" \
  -o "$OUT_DIR/capability_audit_fixture_test"

LOG_PATH="$OUT_DIR/capability_audit_fixture_test.log"
"$OUT_DIR/capability_audit_fixture_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:capability_audit_fixture_bytes_identical" "$LOG_PATH"
grep -q "TEST:PASS:capability_audit_fixture_reset_idempotent" "$LOG_PATH"
grep -q "TEST:PASS:capability_audit_fixture$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
