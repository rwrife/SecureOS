#!/usr/bin/env bash
# build/scripts/test_broker_svc_delete_owner_authority_check.sh
#
# Build + run the M5-SUBSTRATE-002 authority-predicate validator
# (issue #324, plan plans/2026-05-25-m5-ownership-on-m1-substrate.md
# slice 2).
#
# Covers:
#   - actor == owner            -> ALLOW, no marker.
#   - actor == SUBJECT_M5_ADMIN -> ALLOW, no marker.
#   - bystander                  -> DENY, exact canonical CAP:DENY shape.
#   - Captured marker round-trips through cap_deny_marker_validate
#     (#221 / #244 conformance grammar).
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh and validate_bundle.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_broker.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/ipc/ipc_port.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/kernel/svc/broker_svc.c" \
  "$ROOT_DIR/tests/harness/svc_subjects.c" \
  "$ROOT_DIR/tests/broker_svc_delete_owner_authority_check_test.c" \
  -o "$OUT_DIR/broker_svc_delete_owner_authority_check_test"

LOG_PATH="$OUT_DIR/broker_svc_delete_owner_authority_check_test.log"
"$OUT_DIR/broker_svc_delete_owner_authority_check_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:broker_svc_delete_owner_authority_check_self_allow"        "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_delete_owner_authority_check_admin_allow"       "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_delete_owner_authority_check_bystander_deny"    "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_delete_owner_authority_check_deny_marker_grammar" "$LOG_PATH"
grep -q "TEST:PASS:broker_svc_delete_owner_authority_check$"                  "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
