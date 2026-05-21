#!/usr/bin/env bash
#
# test_workflow_rule.sh — Validator dispatcher for the workflow rule
# slice (issue #77 / plan 2026-04-08-zero-trust-workflow-rule-hardening).
#
# Compiles `tests/workflow_rule_test.c` against the new
# `kernel/cap/workflow_rule.c` translation unit and asserts every
# expected TEST:PASS marker plus the TEST:DONE sentinel.
#
# Launched by build/scripts/test.sh (target: workflow_rule).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/workflow_rule.c" \
  "$ROOT_DIR/tests/workflow_rule_test.c" \
  -o "$OUT_DIR/workflow_rule_test"

LOG_PATH="$OUT_DIR/workflow_rule_test.log"
"$OUT_DIR/workflow_rule_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:workflow_rule_allow_path"           "$LOG_PATH"
grep -q "TEST:PASS:workflow_rule_cross_tenant_deny"    "$LOG_PATH"
grep -q "TEST:PASS:workflow_rule_scope_mismatch_deny"  "$LOG_PATH"
grep -q "TEST:PASS:workflow_rule_audit_non_interference" "$LOG_PATH"
grep -q "TEST:PASS:workflow_rule_input_validation"     "$LOG_PATH"
grep -q "TEST:PASS:workflow_rule_capacity"             "$LOG_PATH"
grep -q "TEST:PASS:workflow_rule_audit_format"         "$LOG_PATH"
grep -q "TEST:DONE:workflow_rule"                      "$LOG_PATH"
