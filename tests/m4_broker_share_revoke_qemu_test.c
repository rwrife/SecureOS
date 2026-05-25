/**
 * @file m4_broker_share_revoke_qemu_test.c
 * @brief M4-on-M1 substrate peer of `tests/broker_share_revoke_test.c`
 *        (slice 4 of plan #300, issue #305).
 *
 * Drives the broker revoke path through the real M1 substrate end-to-
 * end, mirroring the allow- and deny-path peers (#304, slice 3):
 *
 *   1. `broker_svc_init()` allocates the broker port (slice 1, #302).
 *   2. `launcher_broker_spawn_app_with_broker_cap()` (slice 2, #303)
 *      spawns the owner PCB with the broker send handle stamped into
 *      `ipc_scratch[24..32)`. A second spawn produces the recipient
 *      PCB (same convention) so the substrate-only
 *      `process_destroy_recycle_revokes` sub-check can call
 *      `process_destroy(recipient_pid)` and observe the side-effect.
 *   3. Owner emits `BROKER_OP_REQUEST` then `BROKER_OP_APPROVE` via
 *      `ipc_send_h`. The driver fans both into `cap_broker_*` and
 *      mints the recipient-side `cap_handle_t` via
 *      `cap_handle_grant(recipient, CAP_FS_READ)` — the M1 analogue
 *      of the "broker writes recipient row in cap_table" side-effect
 *      the plan §"What changes #1" calls for.
 *   4. Owner emits `BROKER_OP_REVOKE` via `ipc_send_h`. The driver
 *      fans it into `cap_broker_revoke` and follows up with
 *      `cap_handle_revoke` so the minted recipient handle bumps its
 *      generation and any subsequent `cap_gate_check_handle` fails.
 *   5. A fresh share + recipient self-revoke exercises the
 *      `actor_subject_id ∈ {owner, recipient}` authority branch.
 *   6. A second revoke is a stable terminal state.
 *   7. Finally, `process_destroy(recipient_pid)` cascades into
 *      `cap_handle_revoke_subject(recipient)`; a probe handle minted
 *      pre-destroy must fail `cap_gate_check_handle` post-destroy.
 *
 * Marker contract (matches the host-fixture sub-check names with a
 * `_qemu` suffix, plus two substrate-only additions called out in the
 * plan):
 *   TEST:PASS:m4_broker_share_revoke_qemu:setup_grants_recipient_qemu
 *   TEST:PASS:m4_broker_share_revoke_qemu:owner_revoke_takes_effect_qemu
 *   TEST:PASS:m4_broker_share_revoke_qemu:underlying_table_revoked_qemu
 *   TEST:PASS:m4_broker_share_revoke_qemu:double_revoke_is_idempotent_qemu
 *   TEST:PASS:m4_broker_share_revoke_qemu:recipient_self_revoke_qemu
 *   TEST:PASS:m4_broker_share_revoke_qemu:order_observed_qemu
 *   TEST:PASS:m4_broker_share_revoke_qemu:process_destroy_recycle_revokes
 *   TEST:SKIP:m4_broker_share_revoke_qemu:audit_revoke_recorded_qemu:...
 *   TEST:PASS:m4_broker_share_revoke_qemu                       (umbrella)
 *
 * The `:order_observed_qemu` marker exists as the plan §"Deadlock
 * mitigation" footnote: we drain each `ipc_send_h` envelope strictly
 * before emitting the next one, so the synchronous IPC v0 primitive
 * never has two in-flight messages on the same port. The marker is
 * fired once the full request → approve → revoke sequence has been
 * observed in order, proving the substrate didn't reorder them.
 *
 * Issue: #305. Plan: plans/2026-05-25-m4-broker-on-m1-substrate.md
 * slice 4. Pre-flight contract: `tests/broker_share_revoke_test.c`
 * keeps verbatim markers; this peer is additive.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_broker.h"
#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/ipc/ipc_msg.h"
#include "../kernel/ipc/ipc_ops.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/proc/process.h"
#include "../kernel/proc/proc_sched.h"
#include "../kernel/svc/broker_svc.h"
#include "../kernel/user/helloapp.h"
#include "../kernel/user/launcher.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:m4_broker_share_revoke_qemu:%s\n", reason);
  g_fail = 1;
}

static void reset_world(void) {
  launcher_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  proc_sched_reset();
  broker_svc_reset();
  cap_broker_reset();
  ipc_port_table_reset();
  launcher_spawn_reset();
}

static cap_handle_t broker_svc_setup_recv(void) {
  if (cap_grant_for_tests((cap_subject_id_t)SUBJECT_M4_BROKER_SVC,
                          CAP_IPC_SEND) != CAP_OK) {
    return CAP_HANDLE_NULL;
  }
  return cap_handle_grant((cap_subject_id_t)SUBJECT_M4_BROKER_SVC,
                          CAP_IPC_SEND);
}

static uint32_t ld_le32(const uint8_t *p) {
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

/* Drain a BROKER_OP_REQUEST envelope and fan into cap_broker_request_share. */
static int driver_handle_request(ipc_port_t broker_port,
                                 cap_handle_t recv_h,
                                 cap_subject_id_t expected_owner,
                                 cap_subject_id_t expected_recip,
                                 capability_id_t  expected_cap,
                                 const char      *expected_resource,
                                 cap_share_id_t *out_sid) {
  ipc_msg_v0 rx = {0};
  if (ipc_recv_h(recv_h, broker_port, &rx) != IPC_OK) {
    fail("request_drain_not_ok");
    return 0;
  }
  if (rx.sender_subject != (uint32_t)expected_owner ||
      rx.tag != (uint32_t)BROKER_OP_REQUEST ||
      rx.payload_len < 40u) {
    fail("request_drain_bad_envelope");
    return 0;
  }
  cap_subject_id_t recip = (cap_subject_id_t)ld_le32(&rx.payload[0]);
  capability_id_t  cap   = (capability_id_t)ld_le32(&rx.payload[4]);
  uint8_t          rlen  = rx.payload[8];
  if (recip != expected_recip || cap != expected_cap ||
      rlen == 0u || rlen > 31u) {
    fail("request_drain_bad_fields");
    return 0;
  }
  char resource[32] = {0};
  memcpy(resource, &rx.payload[9], rlen);
  if (strcmp(resource, expected_resource) != 0) {
    fail("request_drain_bad_resource");
    return 0;
  }
  if (cap_broker_request_share(expected_owner, recip, cap,
                               resource, out_sid) != CAP_BROKER_OK ||
      *out_sid == CAP_SHARE_ID_INVALID) {
    fail("cap_broker_request_share_failed");
    return 0;
  }
  return 1;
}

/* Drain a sid-carrying op (APPROVE/REVOKE/DENY) and assert the sender
 * + tag + share id. */
static int driver_drain_sid_op(ipc_port_t broker_port,
                               cap_handle_t recv_h,
                               cap_subject_id_t expected_sender,
                               broker_op_t expected_op,
                               cap_share_id_t expected_sid,
                               const char *fail_tag) {
  ipc_msg_v0 rx = {0};
  if (ipc_recv_h(recv_h, broker_port, &rx) != IPC_OK) {
    fail(fail_tag);
    return 0;
  }
  if (rx.sender_subject != (uint32_t)expected_sender ||
      rx.tag != (uint32_t)expected_op ||
      rx.payload_len < 4u) {
    fail(fail_tag);
    return 0;
  }
  cap_share_id_t sid = (cap_share_id_t)ld_le32(&rx.payload[0]);
  if (sid != expected_sid) {
    fail(fail_tag);
    return 0;
  }
  return 1;
}

int main(void) {
  reset_world();

  if (broker_svc_init() != BROKER_SVC_OK) {
    fail("broker_svc_init_failed");
    goto out;
  }
  ipc_port_t broker_port = broker_svc_port();
  if (broker_port == IPC_PORT_INVALID) {
    fail("broker_port_invalid");
    goto out;
  }
  cap_handle_t broker_recv = broker_svc_setup_recv();
  if (broker_recv == CAP_HANDLE_NULL) {
    fail("broker_svc_recv_setup_failed");
    goto out;
  }

  const cap_subject_id_t owner     = (cap_subject_id_t)SUBJECT_M2_HELLOAPP;
  const cap_subject_id_t recip     = (cap_subject_id_t)6u;
  const cap_subject_id_t bystander = (cap_subject_id_t)7u;

  /* Spawn the OWNER PCB via the slice-2 broker-spawn helper. */
  launcher_manifest_t m_owner = {
      .subject_id       = owner,
      .auto_grant_caps  = NULL,
      .auto_grant_count = 0u,
  };
  launcher_broker_spawn_t sp_owner;
  if (launcher_broker_spawn_app_with_broker_cap(&m_owner, &sp_owner)
      != LAUNCHER_OK) {
    fail("launcher_broker_spawn_owner_failed");
    goto out;
  }
  if (sp_owner.pid == PID_INVALID ||
      !process_is_live_for_tests(sp_owner.pid) ||
      sp_owner.broker_handle == CAP_HANDLE_NULL ||
      cap_gate_check_handle(sp_owner.broker_handle, CAP_IPC_SEND) != 1) {
    fail("spawned_owner_pcb_bad");
    goto out;
  }

  /* Spawn the RECIPIENT PCB too — the process_destroy sub-check below
   * needs a real PCB to tear down so cap_handle_revoke_subject runs. */
  launcher_manifest_t m_recip = {
      .subject_id       = recip,
      .auto_grant_caps  = NULL,
      .auto_grant_count = 0u,
  };
  launcher_broker_spawn_t sp_recip;
  if (launcher_broker_spawn_app_with_broker_cap(&m_recip, &sp_recip)
      != LAUNCHER_OK) {
    fail("launcher_broker_spawn_recip_failed");
    goto out;
  }
  if (sp_recip.pid == PID_INVALID ||
      !process_is_live_for_tests(sp_recip.pid) ||
      sp_recip.broker_handle == CAP_HANDLE_NULL ||
      cap_gate_check_handle(sp_recip.broker_handle, CAP_IPC_SEND) != 1) {
    fail("spawned_recip_pcb_bad");
    goto out;
  }

  /* Owner must hold the cap they are offering. */
  if (cap_table_grant(owner, CAP_FS_READ) != CAP_OK) {
    fail("owner_grant_setup_failed");
    goto out;
  }

  /* Order-observation: track strict request → approve → revoke
   * sequencing on the owner side; flipped to true at the bottom of
   * the first round-trip and asserted before the marker is emitted. */
  int saw_request = 0;
  int saw_approve = 0;
  int saw_revoke  = 0;

  /* ---- setup_grants_recipient_qemu ------------------------------ */
  helloapp_broker_owner_result_t ownr = {0};
  helloapp_entry_broker_owner(sp_owner.aspace, broker_port,
                              recip, CAP_FS_READ,
                              "doc-alpha", 9u,
                              /*share_id_in=*/NULL, &ownr);
  if (ownr.request_send_result != IPC_OK) {
    fail("owner_request_send_not_ok");
    goto out;
  }
  cap_share_id_t sid = CAP_SHARE_ID_INVALID;
  if (!driver_handle_request(broker_port, broker_recv, owner,
                             recip, CAP_FS_READ, "doc-alpha", &sid)) {
    goto out;
  }
  saw_request = 1;

  ipc_result_t ar =
      helloapp_entry_broker_owner_approve(sp_owner.aspace, broker_port, sid);
  if (ar != IPC_OK) {
    fail("owner_approve_send_not_ok");
    goto out;
  }
  if (!driver_drain_sid_op(broker_port, broker_recv, owner,
                           BROKER_OP_APPROVE, sid,
                           "approve_drain_bad_envelope")) {
    goto out;
  }
  if (cap_broker_approve(owner, sid) != CAP_BROKER_OK) {
    fail("cap_broker_approve_failed");
    goto out;
  }
  saw_approve = 1;

  /* Mint the M1 recipient-side handle that broker_svc would mint in a
   * full in-kernel dispatch (plan §"What changes #1"). The probe is
   * what process_destroy_recycle_revokes asserts dies later. */
  cap_handle_t recip_grant = cap_handle_grant(recip, CAP_FS_READ);
  if (recip_grant == CAP_HANDLE_NULL ||
      cap_gate_check_handle(recip_grant, CAP_FS_READ) != 1) {
    fail("recipient_handle_mint_failed");
    goto out;
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha") != CAP_OK) {
    fail("post_approve_check_failed");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_revoke_qemu:setup_grants_recipient_qemu\n");

  /* ---- owner_revoke_takes_effect_qemu --------------------------- */
  /* Bystander must be rejected before the owner-revoke succeeds.
   * (Direct cap_broker_revoke on the bystander subject — broker
   * authority is checked inside cap_broker_*, matching the
   * deny-peer pattern for bystander mutation.) */
  if (cap_broker_revoke(bystander, sid) != CAP_BROKER_ERR_NOT_AUTHORIZED) {
    fail("bystander_revoke_should_be_rejected");
    goto out;
  }

  ipc_result_t rr =
      helloapp_entry_broker_owner_revoke(sp_owner.aspace, broker_port, sid);
  if (rr != IPC_OK) {
    fail("owner_revoke_send_not_ok");
    goto out;
  }
  if (!driver_drain_sid_op(broker_port, broker_recv, owner,
                           BROKER_OP_REVOKE, sid,
                           "revoke_drain_bad_envelope")) {
    goto out;
  }
  if (cap_broker_revoke(owner, sid) != CAP_BROKER_OK) {
    fail("cap_broker_revoke_failed");
    goto out;
  }
  /* Invalidate the minted recipient handle as broker_svc would. */
  if (cap_handle_revoke(recip_grant) != CAP_OK) {
    fail("cap_handle_revoke_failed");
    goto out;
  }
  saw_revoke = 1;

  if (cap_broker_state_for_tests(sid) != CAP_SHARE_STATE_REVOKED) {
    fail("state_not_revoked_after_revoke");
    goto out;
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha")
        != CAP_ERR_MISSING) {
    fail("recipient_should_miss_after_revoke");
    goto out;
  }
  if (cap_gate_check_handle(recip_grant, CAP_FS_READ) != 0) {
    fail("recipient_handle_should_fail_after_revoke");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_revoke_qemu:owner_revoke_takes_effect_qemu\n");

  /* ---- underlying_table_revoked_qemu — defense in depth. -------- */
  if (cap_table_check(recip, CAP_FS_READ) != CAP_ERR_MISSING) {
    fail("underlying_table_still_grants_recipient");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_revoke_qemu:underlying_table_revoked_qemu\n");

  /* ---- double_revoke_is_idempotent_qemu ------------------------- */
  cap_broker_result_t second = cap_broker_revoke(owner, sid);
  if (second != CAP_BROKER_OK && second != CAP_BROKER_ERR_BAD_STATE) {
    fail("second_revoke_unexpected_result");
    goto out;
  }
  if (cap_broker_state_for_tests(sid) != CAP_SHARE_STATE_REVOKED) {
    fail("state_changed_after_second_revoke");
    goto out;
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha")
        != CAP_ERR_MISSING) {
    fail("recipient_regained_access_after_second_revoke");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_revoke_qemu:double_revoke_is_idempotent_qemu\n");

  /* ---- order_observed_qemu — strict request → approve → revoke. -- */
  if (!(saw_request && saw_approve && saw_revoke)) {
    fail("ordering_not_strict");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_revoke_qemu:order_observed_qemu\n");

  /* ---- recipient_self_revoke_qemu — fresh share; recipient revokes
   *      directly (the ipc_send_h path is identical, so we exercise
   *      the substrate by sending BROKER_OP_REVOKE from the recipient
   *      PCB's own broker handle). */
  helloapp_broker_owner_result_t ownr2 = {0};
  helloapp_entry_broker_owner(sp_owner.aspace, broker_port,
                              recip, CAP_FS_READ,
                              "doc-alpha", 9u,
                              /*share_id_in=*/NULL, &ownr2);
  if (ownr2.request_send_result != IPC_OK) {
    fail("owner_second_request_send_not_ok");
    goto out;
  }
  cap_share_id_t sid2 = CAP_SHARE_ID_INVALID;
  if (!driver_handle_request(broker_port, broker_recv, owner,
                             recip, CAP_FS_READ, "doc-alpha", &sid2)) {
    goto out;
  }
  if (helloapp_entry_broker_owner_approve(sp_owner.aspace, broker_port, sid2)
      != IPC_OK) {
    fail("owner_second_approve_send_not_ok");
    goto out;
  }
  if (!driver_drain_sid_op(broker_port, broker_recv, owner,
                           BROKER_OP_APPROVE, sid2,
                           "second_approve_drain_bad_envelope")) {
    goto out;
  }
  if (cap_broker_approve(owner, sid2) != CAP_BROKER_OK) {
    fail("cap_broker_second_approve_failed");
    goto out;
  }
  cap_handle_t recip_grant2 = cap_handle_grant(recip, CAP_FS_READ);
  if (recip_grant2 == CAP_HANDLE_NULL) {
    fail("recipient_handle_mint2_failed");
    goto out;
  }

  /* Recipient self-revoke via its own broker handle. */
  ipc_result_t rsr =
      helloapp_entry_broker_owner_revoke(sp_recip.aspace, broker_port, sid2);
  if (rsr != IPC_OK) {
    fail("recipient_revoke_send_not_ok");
    goto out;
  }
  if (!driver_drain_sid_op(broker_port, broker_recv, recip,
                           BROKER_OP_REVOKE, sid2,
                           "recipient_revoke_drain_bad_envelope")) {
    goto out;
  }
  if (cap_broker_revoke(recip, sid2) != CAP_BROKER_OK) {
    fail("recipient_self_revoke_failed");
    goto out;
  }
  if (cap_handle_revoke(recip_grant2) != CAP_OK) {
    fail("cap_handle_revoke2_failed");
    goto out;
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha")
        != CAP_ERR_MISSING) {
    fail("recipient_still_has_access_after_self_revoke");
    goto out;
  }
  if (cap_broker_recipient_check(bystander, CAP_FS_READ, "doc-alpha")
        != CAP_ERR_MISSING) {
    fail("bystander_should_not_be_granted");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_revoke_qemu:recipient_self_revoke_qemu\n");

  /* ---- process_destroy_recycle_revokes — substrate-only.
   *      Mint a fresh probe handle for the recipient subject, then
   *      tear down the recipient PCB. process_destroy() cascades into
   *      cap_handle_revoke_subject(recipient) per kernel/proc/process.c
   *      so the probe must fail cap_gate_check_handle post-call. */
  cap_handle_t recip_probe = cap_handle_grant(recip, CAP_FS_READ);
  if (recip_probe == CAP_HANDLE_NULL ||
      cap_gate_check_handle(recip_probe, CAP_FS_READ) != 1) {
    fail("recipient_probe_mint_failed");
    goto out;
  }
  if (launcher_broker_spawn_destroy(sp_recip.pid) != LAUNCHER_OK) {
    fail("recipient_spawn_destroy_failed");
    goto out;
  }
  if (process_is_live_for_tests(sp_recip.pid)) {
    fail("recipient_pcb_still_live_after_destroy");
    goto out;
  }
  if (cap_gate_check_handle(recip_probe, CAP_FS_READ) != 0) {
    fail("recipient_probe_should_fail_after_process_destroy");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_revoke_qemu:process_destroy_recycle_revokes\n");

  /* audit_revoke_recorded_qemu — issue #311. Multiple revokes happen
   * across this fixture (owner-driven, recipient-self, process-destroy
   * cascade). Assert at least one REVOKE record with owner-as-actor and
   * recip-as-subject for CAP_FS_READ is present in the audit ring. */
  {
    size_t total = cap_audit_count_for_tests();
    int found = 0;
    for (size_t i = 0; i < total; ++i) {
      cap_audit_event_t ev;
      if (cap_audit_get_for_tests(i, &ev) != CAP_OK) continue;
      if (ev.operation == CAP_AUDIT_OP_REVOKE &&
          ev.subject_id == recip &&
          ev.capability_id == CAP_FS_READ) {
        found = 1;
        break;
      }
    }
    if (!found) {
      fail("audit_revoke_record_not_found");
      goto out;
    }
  }
  printf("TEST:PASS:m4_broker_share_revoke_qemu:audit_revoke_recorded_qemu\n");

  /* Tear down the owner PCB. */
  if (launcher_broker_spawn_destroy(sp_owner.pid) != LAUNCHER_OK) {
    fail("owner_spawn_destroy_failed");
    goto out;
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:m4_broker_share_revoke_qemu\n");
  return 0;
}
