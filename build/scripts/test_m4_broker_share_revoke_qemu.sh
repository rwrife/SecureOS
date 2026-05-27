#!/usr/bin/env bash
# build/scripts/test_m4_broker_share_revoke_qemu.sh
#
# Build + run the M4-on-M1 substrate revoke-path peer of
# broker_share_revoke_test.c (issue #305, plan
# plans/2026-05-25-m4-broker-on-m1-substrate.md slice 4).
#
# Covers:
#   - helloapp_entry_broker_owner() sends BROKER_OP_REQUEST via the
#     real ipc_send_h path on the broker handle stamped by
#     launcher_broker_spawn_app_with_broker_cap() into
#     ipc_scratch[24..32).
#   - helloapp_entry_broker_owner_approve() sends BROKER_OP_APPROVE
#     and helloapp_entry_broker_owner_revoke() sends BROKER_OP_REVOKE.
#   - The test driver drains each envelope via ipc_recv_h and fans
#     them into cap_broker_*, asserting owner-revoke-takes-effect,
#     recipient-self-revoke, underlying-table-revoked,
#     double-revoke-is-idempotent, and order-observed.
#   - A substrate-only assertion calls process_destroy() on the
#     recipient PCB and observes that cap_handle_revoke_subject()
#     invalidates a prior recipient handle.
#   - audit_revoke_recorded_qemu asserts a broker-revoke audit record
#     is emitted into the cap_audit ring (wired via #311).
#
# Emits the following deterministic markers (consumed by
# build/scripts/test.sh and validate_bundle.sh):
#   TEST:PASS:m4_broker_share_revoke_qemu:setup_grants_recipient_qemu
#   TEST:PASS:m4_broker_share_revoke_qemu:owner_revoke_takes_effect_qemu
#   TEST:PASS:m4_broker_share_revoke_qemu:underlying_table_revoked_qemu
#   TEST:PASS:m4_broker_share_revoke_qemu:double_revoke_is_idempotent_qemu
#   TEST:PASS:m4_broker_share_revoke_qemu:order_observed_qemu
#   TEST:PASS:m4_broker_share_revoke_qemu:recipient_self_revoke_qemu
#   TEST:PASS:m4_broker_share_revoke_qemu:process_destroy_recycle_revokes
#   TEST:PASS:m4_broker_share_revoke_qemu:audit_revoke_recorded_qemu
#   TEST:PASS:m4_broker_share_revoke_qemu

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
  "$ROOT_DIR/kernel/user/launcher.c" \
  "$ROOT_DIR/kernel/user/helloapp.c" \
  "$ROOT_DIR/tests/harness/svc_subjects.c" \
  "$ROOT_DIR/tests/harness/session_manager_stub.c" \
  "$ROOT_DIR/tests/m4_broker_share_revoke_qemu_test.c" \
  -o "$OUT_DIR/m4_broker_share_revoke_qemu_test"

LOG_PATH="$OUT_DIR/m4_broker_share_revoke_qemu_test.log"
"$OUT_DIR/m4_broker_share_revoke_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:m4_broker_share_revoke_qemu:setup_grants_recipient_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_revoke_qemu:owner_revoke_takes_effect_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_revoke_qemu:underlying_table_revoked_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_revoke_qemu:double_revoke_is_idempotent_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_revoke_qemu:order_observed_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_revoke_qemu:recipient_self_revoke_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_revoke_qemu:process_destroy_recycle_revokes" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_revoke_qemu:audit_revoke_recorded_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_revoke_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
