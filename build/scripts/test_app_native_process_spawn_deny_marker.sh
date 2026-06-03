#!/usr/bin/env bash
# Build + run the issue #532 app_native_process_spawn CAP:DENY marker test.
#
# Pins the byte-exact `CAP:DENY:<sid>:app_exec:<resource>\n` line
# emitted by `app_native_spawn_cap_check` (extracted from the
# `app_native_process_spawn` bridge slot in
# `kernel/user/launcher_exec.c`, M7-TOOLCHAIN-003 #422 / PR #427) when
# the calling subject lacks `CAP_APP_EXEC`. Same extraction-seam pattern
# PR #495 used for `app_native_mem_brk` (#421).
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
  "$ROOT_DIR/kernel/user/app_native_spawn.c" \
  "$ROOT_DIR/tests/app_native_process_spawn_deny_marker_test.c" \
  -o "$OUT_DIR/app_native_process_spawn_deny_marker_test"

LOG_PATH="$OUT_DIR/app_native_process_spawn_deny_marker_test.log"
"$OUT_DIR/app_native_process_spawn_deny_marker_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:app_native_process_spawn_deny_marker_canonical_shape" "$LOG_PATH"
grep -q "TEST:PASS:app_native_process_spawn_deny_marker_sanitizer" "$LOG_PATH"
grep -q "TEST:PASS:app_native_process_spawn_deny_marker_empty_path" "$LOG_PATH"
grep -q "TEST:PASS:app_native_process_spawn_deny_marker$" "$LOG_PATH"
