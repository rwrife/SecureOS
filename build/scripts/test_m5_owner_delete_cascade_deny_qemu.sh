#!/usr/bin/env bash
# build/scripts/test_m5_owner_delete_cascade_deny_qemu.sh
#
# Build + run the M5-on-M1 substrate deny-path peer for
# BROKER_OP_DELETE_OWNER (issue #326, plan
# plans/2026-05-25-m5-ownership-on-m1-substrate.md slice 4).
#
# Companion to test_m5_owner_delete_cascade_allow_qemu.sh (slice 3 /
# issue #325). Same build recipe; only the test TU differs. Asserts
# three deny-side invariants over the cascade:
#
#   - bystander_cannot_delete_owner          : authority gate denies a
#                                              third-subject caller and
#                                              emits the canonical
#                                              CAP:DENY:7:capability_admin:
#                                              delete_owner_3 marker.
#   - double_delete_is_idempotent            : second self-delete is a
#                                              clean no-op (n_children=0).
#   - process_destroy_recycle_revokes_qemu   : recycling the owner
#                                              subject id via
#                                              process_create does NOT
#                                              resurrect previously
#                                              revoked handles.
#
# Emits:
#   TEST:PASS:m5_owner_delete_cascade_deny_qemu:bystander_cannot_delete_owner
#   TEST:PASS:m5_owner_delete_cascade_deny_qemu:audit_deny_recorded_no_cascade_qemu
#   TEST:PASS:m5_owner_delete_cascade_deny_qemu:double_delete_is_idempotent
#   TEST:PASS:m5_owner_delete_cascade_deny_qemu:process_destroy_recycle_revokes_qemu
#   TEST:PASS:m5_owner_delete_cascade_deny_qemu
# plus the explicit deny-marker grep below.

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
  "$ROOT_DIR/tests/m5_owner_delete_cascade_deny_qemu_test.c" \
  -o "$OUT_DIR/m5_owner_delete_cascade_deny_qemu_test"

LOG_PATH="$OUT_DIR/m5_owner_delete_cascade_deny_qemu_test.log"
"$OUT_DIR/m5_owner_delete_cascade_deny_qemu_test" | tee "$LOG_PATH"

# Canonical CAP:DENY marker must appear at least once: subject 7
# (bystander) attempting to delete owner subject 3 surfaces through
# the capability_admin reuse + "delete_owner_<owner>" resource string
# (see kernel/svc/broker_svc.c cap_broker_delete_owner_check).
grep -q "^CAP:DENY:7:capability_admin:delete_owner_3$" "$LOG_PATH"

grep -q "TEST:PASS:m5_owner_delete_cascade_deny_qemu:bystander_cannot_delete_owner" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_deny_qemu:audit_deny_recorded_no_cascade_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_deny_qemu:double_delete_is_idempotent" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_deny_qemu:process_destroy_recycle_revokes_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m5_owner_delete_cascade_deny_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
