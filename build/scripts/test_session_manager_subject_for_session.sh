#!/usr/bin/env bash
# build/scripts/test_session_manager_subject_for_session.sh
#
# Compiles and runs tests/session_manager_subject_for_session_test.c
# against the `session_manager_subject_for_session` accessor added in
# kernel/core/session_manager.c (issue #375, follow-up to #349 / PR #365).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -DSECUREOS_HOST_TEST=1 \
  "$ROOT_DIR/kernel/core/session_manager.c" \
  "$ROOT_DIR/tests/session_manager_subject_for_session_test.c" \
  -o "$OUT_DIR/session_manager_subject_for_session_test"

LOG_PATH="$OUT_DIR/session_manager_subject_for_session_test.log"
"$OUT_DIR/session_manager_subject_for_session_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:session_manager_subject_for_session:out_of_range" "$LOG_PATH"
grep -q "TEST:PASS:session_manager_subject_for_session:unused_slot" "$LOG_PATH"
grep -q "TEST:PASS:session_manager_subject_for_session:in_use_slot" "$LOG_PATH"
grep -q "TEST:PASS:session_manager_subject_for_session:null_out_param" "$LOG_PATH"
grep -q "TEST:PASS:session_manager_subject_for_session:roundtrip" "$LOG_PATH"
grep -q "TEST:PASS:session_manager_subject_for_session$" "$LOG_PATH"
