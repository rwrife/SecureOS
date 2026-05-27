#!/usr/bin/env bash
# build/scripts/test_session_manager_first_for_subject.sh
#
# Compiles and runs tests/session_manager_first_for_subject_test.c
# against the M5-SUBSTRATE-005a enumerator predicate
# `session_manager_first_session_for_subject` added in
# kernel/core/session_manager.c (issue #350, plan
# plans/2026-05-26-m5-wm-cascade-on-substrate.md).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -DSECUREOS_HOST_TEST=1 \
  "$ROOT_DIR/kernel/core/session_manager.c" \
  "$ROOT_DIR/tests/session_manager_first_for_subject_test.c" \
  -o "$OUT_DIR/session_manager_first_for_subject_test"

LOG_PATH="$OUT_DIR/session_manager_first_for_subject_test.log"
"$OUT_DIR/session_manager_first_for_subject_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:session_manager_first_for_subject:no_sessions" "$LOG_PATH"
grep -q "TEST:PASS:session_manager_first_for_subject:single" "$LOG_PATH"
grep -q "TEST:PASS:session_manager_first_for_subject:drain" "$LOG_PATH"
grep -q "TEST:PASS:session_manager_first_for_subject:owner_isolation" "$LOG_PATH"
grep -q "TEST:PASS:session_manager_first_for_subject:null_out_param" "$LOG_PATH"
grep -q "TEST:PASS:session_manager_first_for_subject$" "$LOG_PATH"
