#!/usr/bin/env bash
set -euo pipefail

# win_gfx_gates host-side validator for BUILD_ROADMAP.md §5.5/§5.6
# (issue #349). Asserts the kernel-side cap_gfx_framebuffer_gate /
# cap_input_keyboard_gate / cap_input_mouse_gate trio enforces the
# zero-trust deny-by-default contract that the virtual-graphics HAL
# and PS/2 keyboard/mouse drivers MUST call before exposing their
# byte queue / framebuffer mapping to a launched subject.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_gate.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/tests/win_gfx_gates_test.c" \
  -o "$OUT_DIR/win_gfx_gates_test"

LOG_PATH="$OUT_DIR/win_gfx_gates_test.log"
"$OUT_DIR/win_gfx_gates_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:win_gfx_framebuffer_gate_allow" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_framebuffer_gate_deny" "$LOG_PATH"
grep -q "TEST:PASS:win_input_keyboard_gate_allow" "$LOG_PATH"
grep -q "TEST:PASS:win_input_keyboard_gate_deny" "$LOG_PATH"
grep -q "TEST:PASS:win_input_mouse_gate_allow" "$LOG_PATH"
grep -q "TEST:PASS:win_input_mouse_gate_deny" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_gates" "$LOG_PATH"
