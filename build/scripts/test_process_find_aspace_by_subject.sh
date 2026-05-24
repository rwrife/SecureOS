#!/usr/bin/env bash
# Build + run the host-side test for process_find_aspace_by_subject()
# (issue #260 — IPC bounds-check prerequisite helper).
#
# Mirrors test_process_table.sh shape: compile the helper's TU plus its
# transitive cap deps, run, scan log for deterministic PASS markers,
# reject any FAIL marker.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/tests/process_find_aspace_by_subject_test.c" \
  -o "$OUT_DIR/process_find_aspace_by_subject_test"

LOG_PATH="$OUT_DIR/process_find_aspace_by_subject_test.log"
"$OUT_DIR/process_find_aspace_by_subject_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:process_find_aspace_by_subject:hit_returns_stored_aspace" "$LOG_PATH"
grep -q "TEST:PASS:process_find_aspace_by_subject:miss_returns_null" "$LOG_PATH"
grep -q "TEST:PASS:process_find_aspace_by_subject:subject_zero_returns_null" "$LOG_PATH"
grep -q "TEST:PASS:process_find_aspace_by_subject:destroy_then_miss" "$LOG_PATH"
grep -q "TEST:PASS:process_find_aspace_by_subject:multiple_subjects_first_wins" "$LOG_PATH"
grep -q "TEST:PASS:process_find_aspace_by_subject:null_aspace_returns_null" "$LOG_PATH"
grep -q "TEST:PASS:process_find_aspace_by_subject$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
