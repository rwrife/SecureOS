#!/usr/bin/env bash
# build/scripts/test_m5_owner_delete_cascade_window_qemu.sh
#
# Build + run the M5-on-M1 substrate window/session-leg peer for
# BROKER_OP_DELETE_OWNER (issue #387, plan
# plans/2026-05-26-m5-wm-cascade-on-substrate.md slice 005c).
#
# Companion to the cap-leg peers (allow/deny). Same build recipe;
# only the test TU differs. Asserts the WM session-side cascade
# (step 3.5 of broker_svc_delete_owner, implemented in PR #363):
#
#   - owned_sessions_destroyed                : both sessions owned by
#                                               the cascade subject are
#                                               torn down by step 3.5.
#   - bystander_session_preserved             : a session owned by an
#                                               unrelated subject is
#                                               untouched (subject
#                                               filter is load-bearing).
#   - delegated_gfx_caps_invalid              : a CAP_GFX_FRAMEBUFFER
#                                               handle minted as a
#                                               child of the owner's
#                                               broker handle gates as
#                                               CAP_ERR_MISSING after
#                                               the cascade.
#   - session_slot_recyclable                 : a fresh session inject
#                                               for the (still-live)
#                                               owner subject succeeds
#                                               post-cascade.
#   - double_delete_idempotent_session_leg    : a second cascade with
#                                               no owner-scoped
#                                               sessions returns OK
#                                               with n_children == 0.
#   - audit_wm_cascade_recorded               : the audit ring shows at
#                                               least one
#                                               CAP_AUDIT_OP_CASCADE_REVOKE
#                                               event whose subject_id
#                                               is one of the destroyed
#                                               session ids.
#   - audit_wm_cascade_done_recorded          : the audit ring shows a
#                                               terminal
#                                               CAP_AUDIT_OP_CASCADE_DONE
#                                               for the owner.
#
# Emits the following deterministic markers (consumed by
# build/scripts/test.sh and validate_bundle.sh):
#   TEST:PASS:m5_owner_delete_cascade_window_qemu:owned_sessions_destroyed
#   TEST:PASS:m5_owner_delete_cascade_window_qemu:bystander_session_preserved
#   TEST:PASS:m5_owner_delete_cascade_window_qemu:delegated_gfx_caps_invalid
#   TEST:PASS:m5_owner_delete_cascade_window_qemu:session_slot_recyclable
#   TEST:PASS:m5_owner_delete_cascade_window_qemu:double_delete_idempotent_session_leg
#   TEST:PASS:m5_owner_delete_cascade_window_qemu:audit_wm_cascade_recorded
#   TEST:PASS:m5_owner_delete_cascade_window_qemu:audit_wm_cascade_done_recorded
#   TEST:PASS:m5_owner_delete_cascade_window_qemu

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
  "$ROOT_DIR/kernel/cap/cap_broker.c" \
  "$ROOT_DIR/kernel/ipc/ipc_port.c" \
  "$ROOT_DIR/kernel/ipc/ipc_ops.c" \
  "$ROOT_DIR/kernel/proc/address_space.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/kernel/proc/proc_sched.c" \
  "$ROOT_DIR/kernel/svc/broker_svc.c" \
  "$ROOT_DIR/kernel/svc/fs_svc.c" \
  "$ROOT_DIR/kernel/user/launcher.c" \
  "$ROOT_DIR/kernel/user/helloapp.c" \
  "$ROOT_DIR/tests/harness/svc_subjects.c" \
  "$ROOT_DIR/tests/harness/session_manager_stub.c" \
  "$ROOT_DIR/tests/m5_owner_delete_cascade_window_qemu_test.c" \
  -o "$OUT_DIR/m5_owner_delete_cascade_window_qemu_test"

LOG_PATH="$OUT_DIR/m5_owner_delete_cascade_window_qemu_test.log"
"$OUT_DIR/m5_owner_delete_cascade_window_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:m5_owner_delete_cascade_window_qemu:owned_sessions_destroyed" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_window_qemu:bystander_session_preserved" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_window_qemu:delegated_gfx_caps_invalid" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_window_qemu:session_slot_recyclable" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_window_qemu:double_delete_idempotent_session_leg" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_window_qemu:audit_wm_cascade_recorded" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_window_qemu:audit_wm_cascade_done_recorded" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_window_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
