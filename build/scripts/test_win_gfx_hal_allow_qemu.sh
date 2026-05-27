#!/usr/bin/env bash
# build/scripts/test_win_gfx_hal_allow_qemu.sh
#
# Build + run the allow-path substrate peer for the HAL call-site
# gates (issue #376, follow-up to #349 / PR #365).
#
# Covers:
#   - launcher_spawn_app_from_manifest() produces a live PCB owned by
#     SUBJECT_M2_HELLOAPP (M2 substrate path).
#   - With CAP_GFX_FRAMEBUFFER + CAP_INPUT_KEYBOARD + CAP_INPUT_MOUSE
#     post-spawn grants in place, the subject-scoped wrappers
#     video_hal_write_as / input_hal_try_read_char_as /
#     mouse_hal_poll_event_as each return CAP_OK and invoke the
#     underlying backend primitive exactly once.
#   - Audit ring records CAP_AUDIT_OP_CHECK with result = CAP_OK for
#     each of the three caps.
#
# Emits the following deterministic markers:
#   TEST:PASS:win_gfx_hal_allow_qemu:video_allow_backend_called_once
#   TEST:PASS:win_gfx_hal_allow_qemu:input_allow_backend_called_once
#   TEST:PASS:win_gfx_hal_allow_qemu:mouse_allow_backend_called_once
#   TEST:PASS:win_gfx_hal_allow_qemu:audit_check_ok_recorded
#   TEST:PASS:win_gfx_hal_allow_qemu

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/kernel/cap/cap_gate.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/kernel/hal/hal_cap_entry.c" \
  "$ROOT_DIR/kernel/proc/address_space.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/kernel/proc/proc_sched.c" \
  "$ROOT_DIR/kernel/user/launcher.c" \
  "$ROOT_DIR/tests/harness/m2_subjects.c" \
  "$ROOT_DIR/tests/win_gfx_hal_allow_qemu_test.c" \
  -o "$OUT_DIR/win_gfx_hal_allow_qemu_test"

LOG_PATH="$OUT_DIR/win_gfx_hal_allow_qemu_test.log"
"$OUT_DIR/win_gfx_hal_allow_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:win_gfx_hal_allow_qemu:video_allow_backend_called_once" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_hal_allow_qemu:input_allow_backend_called_once" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_hal_allow_qemu:mouse_allow_backend_called_once" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_hal_allow_qemu:audit_check_ok_recorded" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_hal_allow_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
