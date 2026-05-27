#!/usr/bin/env bash
set -euo pipefail

# win_gfx_callsite host-side validator for BUILD_ROADMAP.md §5.5/§5.6
# (issue #349, call-site wiring on top of the #357 gate primitive trio).
# Asserts that the kernel/hal/hal_cap_entry.c wrappers
# (video_hal_write_as, input_hal_try_read_char_as,
# mouse_hal_poll_event_as) invoke their corresponding cap_gate *before*
# delegating to the underlying backend-neutral HAL primitive, and that
# the deny path short-circuits the backend entirely while emitting the
# canonical CAP:DENY:<sid>:<cap>:-\n marker.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_gate.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/kernel/hal/hal_cap_entry.c" \
  "$ROOT_DIR/tests/win_gfx_callsite_test.c" \
  -o "$OUT_DIR/win_gfx_callsite_test"

LOG_PATH="$OUT_DIR/win_gfx_callsite_test.log"
"$OUT_DIR/win_gfx_callsite_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:win_gfx_framebuffer_callsite_allow" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_framebuffer_callsite_deny" "$LOG_PATH"
grep -q "TEST:PASS:win_input_keyboard_callsite_allow" "$LOG_PATH"
grep -q "TEST:PASS:win_input_keyboard_callsite_deny" "$LOG_PATH"
grep -q "TEST:PASS:win_input_mouse_callsite_allow" "$LOG_PATH"
grep -q "TEST:PASS:win_input_mouse_callsite_deny" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_callsite" "$LOG_PATH"
