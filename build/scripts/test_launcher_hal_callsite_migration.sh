#!/usr/bin/env bash
# build/scripts/test_launcher_hal_callsite_migration.sh
#
# Host-side migration test for issue #375: asserts the launcher/console
# HAL call sites route through the subject-scoped wrappers landed in
# #349 / PR #365 (video_hal_write_as, input_hal_try_read_char_as) and
# that the deny path short-circuits the backend primitive.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -DSECUREOS_HOST_TEST=1 \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_gate.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/kernel/hal/hal_cap_entry.c" \
  "$ROOT_DIR/kernel/core/session_manager.c" \
  "$ROOT_DIR/tests/launcher_hal_callsite_migration_test.c" \
  -o "$OUT_DIR/launcher_hal_callsite_migration_test"

LOG_PATH="$OUT_DIR/launcher_hal_callsite_migration_test.log"
"$OUT_DIR/launcher_hal_callsite_migration_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:launcher_input_callsite_allow_via_session_lookup" "$LOG_PATH"
grep -q "TEST:PASS:launcher_input_callsite_deny_short_circuits_backend" "$LOG_PATH"
grep -q "TEST:PASS:launcher_input_callsite_session_lookup_miss" "$LOG_PATH"
grep -q "TEST:PASS:console_video_callsite_allow" "$LOG_PATH"
grep -q "TEST:PASS:console_video_callsite_deny_drops_silently" "$LOG_PATH"
grep -q "TEST:PASS:launcher_hal_callsite_migration$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
