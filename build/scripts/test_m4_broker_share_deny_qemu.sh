#!/usr/bin/env bash
# build/scripts/test_m4_broker_share_deny_qemu.sh
#
# Build + run the M4-on-M1 substrate deny-path peer of
# broker_share_deny_test.c (issue #304, plan
# plans/2026-05-25-m4-broker-on-m1-substrate.md slice 3).
#
# Covers:
#   - helloapp_entry_broker_owner() sends BROKER_OP_REQUEST via the
#     real ipc_send_h path on the broker handle stamped by
#     launcher_broker_spawn_app_with_broker_cap() into
#     ipc_scratch[24..32).
#   - helloapp_entry_broker_owner_deny() sends BROKER_OP_DENY.
#   - The test driver drains both envelopes via ipc_recv_h and fans
#     them into cap_broker_*, asserting deny-blocks-recipient,
#     cannot-be-re-approved, bystander-cannot-mutate, and the two
#     scope_is_* invariants.
#   - audit_deny_recorded_qemu asserts a broker-deny audit record is
#     emitted into the cap_audit ring (wired via #311; same shape as
#     the host fixture).
#
# Emits the following deterministic markers (consumed by
# build/scripts/test.sh and validate_bundle.sh):
#   TEST:PASS:m4_broker_share_deny_qemu:request_returns_pending_share_id_qemu
#   TEST:PASS:m4_broker_share_deny_qemu:deny_blocks_recipient_qemu
#   TEST:PASS:m4_broker_share_deny_qemu:cannot_be_re_approved_qemu
#   TEST:PASS:m4_broker_share_deny_qemu:bystander_cannot_mutate_qemu
#   TEST:PASS:m4_broker_share_deny_qemu:scope_is_resource_bound_qemu
#   TEST:PASS:m4_broker_share_deny_qemu:scope_is_capability_bound_qemu
#   TEST:PASS:m4_broker_share_deny_qemu:audit_deny_recorded_qemu
#   TEST:PASS:m4_broker_share_deny_qemu

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
  "$ROOT_DIR/tests/m4_broker_share_deny_qemu_test.c" \
  -o "$OUT_DIR/m4_broker_share_deny_qemu_test"

LOG_PATH="$OUT_DIR/m4_broker_share_deny_qemu_test.log"
"$OUT_DIR/m4_broker_share_deny_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:m4_broker_share_deny_qemu:request_returns_pending_share_id_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_deny_qemu:deny_blocks_recipient_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_deny_qemu:cannot_be_re_approved_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_deny_qemu:bystander_cannot_mutate_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_deny_qemu:scope_is_resource_bound_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_deny_qemu:scope_is_capability_bound_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_deny_qemu:audit_deny_recorded_qemu" "$LOG_PATH"
grep -q "TEST:PASS:m4_broker_share_deny_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
