#!/usr/bin/env bash
# build/scripts/test_cap_handle_revoke_subject.sh
#
# Compiles and runs tests/cap_handle_revoke_subject_test.c against the
# M1-CAPTBL-003 bulk-revoke-by-owner entry point and the
# process_destroy() hook that calls it (issue #239, plan #197).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/tests/cap_handle_revoke_subject_test.c" \
  -o "$OUT_DIR/cap_handle_revoke_subject_test"

LOG_PATH="$OUT_DIR/cap_handle_revoke_subject_test.log"
"$OUT_DIR/cap_handle_revoke_subject_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:cap_handle_revoke_subject_bulk_one_owner" "$LOG_PATH"
grep -q "TEST:PASS:cap_handle_revoke_subject_isolates_owners" "$LOG_PATH"
grep -q "TEST:PASS:cap_handle_revoke_subject_no_rows" "$LOG_PATH"
grep -q "TEST:PASS:cap_handle_revoke_subject_bad_subject" "$LOG_PATH"
grep -q "TEST:PASS:cap_handle_revoke_subject_process_destroy_hook" "$LOG_PATH"
grep -q "TEST:PASS:cap_handle_revoke_subject$" "$LOG_PATH"
