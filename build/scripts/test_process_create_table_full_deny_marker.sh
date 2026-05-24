#!/usr/bin/env bash
# Build + run the issue #261 PROC_TABLE_FULL CAP:DENY marker test.
#
# Asserts that process_create(), upon hitting PROC_TABLE_MAX, emits a
# byte-exact `CAP:DENY:<subject>:app_exec:proc_table_full\n` line that
# round-trips through cap_deny_marker_validate() (the #221 conformance
# predicate), and that the happy create path stays silent.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh and validate_bundle.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/tests/process_create_table_full_deny_marker_test.c" \
  -o "$OUT_DIR/process_create_table_full_deny_marker_test"

LOG_PATH="$OUT_DIR/process_create_table_full_deny_marker_test.log"
"$OUT_DIR/process_create_table_full_deny_marker_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:process_create_table_full_deny_marker_canonical_shape" "$LOG_PATH"
grep -q "TEST:PASS:process_create_table_full_deny_marker_happy_path_silent" "$LOG_PATH"
grep -q "TEST:PASS:process_create_table_full_deny_marker$" "$LOG_PATH"
