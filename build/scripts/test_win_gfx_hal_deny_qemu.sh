#!/usr/bin/env bash
# build/scripts/test_win_gfx_hal_deny_qemu.sh
#
# Build + run the deny-path substrate peer for the HAL call-site
# gates (issue #376, follow-up to #349 / PR #365).
#
# Covers:
#   - launcher_spawn_app_from_manifest() produces a live PCB owned by
#     SUBJECT_M2_HELLOAPP with NO GFX/INPUT cap grants.
#   - Each of the three subject-scoped HAL wrappers returns
#     CAP_ERR_MISSING, the underlying backend primitive is NOT
#     invoked, and the populated deny_marker_buf passes
#     cap_deny_marker_validate().
#   - Audit ring records CAP_AUDIT_OP_CHECK with
#     result = CAP_ERR_MISSING for each of the three caps.
#
# Emits the following deterministic markers:
#   TEST:PASS:win_gfx_hal_deny_qemu:video_deny_backend_not_called
#   TEST:PASS:win_gfx_hal_deny_qemu:input_deny_backend_not_called
#   TEST:PASS:win_gfx_hal_deny_qemu:mouse_deny_backend_not_called
#   TEST:PASS:win_gfx_hal_deny_qemu:deny_marker_conformant
#   TEST:PASS:win_gfx_hal_deny_qemu:audit_check_missing_recorded
#   TEST:PASS:win_gfx_hal_deny_qemu

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
  "$ROOT_DIR/tests/win_gfx_hal_deny_qemu_test.c" \
  -o "$OUT_DIR/win_gfx_hal_deny_qemu_test"

LOG_PATH="$OUT_DIR/win_gfx_hal_deny_qemu_test.log"
"$OUT_DIR/win_gfx_hal_deny_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:win_gfx_hal_deny_qemu:video_deny_backend_not_called" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_hal_deny_qemu:input_deny_backend_not_called" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_hal_deny_qemu:mouse_deny_backend_not_called" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_hal_deny_qemu:deny_marker_conformant" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_hal_deny_qemu:audit_check_missing_recorded" "$LOG_PATH"
grep -q "TEST:PASS:win_gfx_hal_deny_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
