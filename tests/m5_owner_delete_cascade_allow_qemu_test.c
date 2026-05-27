/**
 * @file m5_owner_delete_cascade_allow_qemu_test.c
 * @brief M5-on-M1 substrate peer — `BROKER_OP_DELETE_OWNER` allow-path
 *        cascade (slice 3 of plan #319, issue #325).
 *
 * Rides on the merged M1/M2/M3/M4 substrate plus the slice-001/002
 * cascade plumbing:
 *
 *   1. `broker_svc_init()` allocates the well-known broker port
 *      (slice 1, #302). `fs_svc_init()` allocates the fs read port
 *      so we have a real, CAP_FS_READ-gated target to point a
 *      delegated handle at (mirrors the `delegated_caps_invalid`
 *      bullet in #325).
 *   2. `launcher_broker_spawn_app_with_broker_cap()` (slice 2, #303)
 *      produces a live owner PCB (subject 3) with the broker send
 *      handle stamped LE64 into `ipc_scratch[24..32)`.
 *   3. Owner sends `BROKER_OP_REQUEST` + `BROKER_OP_APPROVE` over the
 *      real `ipc_send_h` path. The driver drains both envelopes;
 *      instead of `cap_broker_approve` (which mints an unparented
 *      handle), it calls `broker_svc_approve_h(... owner_broker_h ...)`
 *      from slice 002 so the recipient handle is parented on the
 *      owner's broker-port handle — i.e. inside the subtree the
 *      `BROKER_OP_DELETE_OWNER` cascade walks.
 *   4. `broker_svc_delete_owner(owner, owner, owner_broker_h,
 *      PID_INVALID, &n)` runs the six-step cascade with the PCB
 *      teardown intentionally suppressed (`PID_INVALID`) so the
 *      `subtree_revoked_before_destroy_qemu` sub-check pins the
 *      ordering guarantee from `kernel/svc/broker_svc.h` §"steps
 *      3 → 4": the subtree walker (step 3) bumps generations
 *      BEFORE `process_destroy`'s bulk subject-revoke (step 4)
 *      could mask the source-of-truth.
 *   5. Post-cascade asserts:
 *        a. `cap_gate_check_handle_result(recipient_h, CAP_FS_READ)`
 *           returns `CAP_ERR_MISSING`. (The issue body wrote
 *           `CAP_ERR_REVOKED`, but `kernel/cap/capability.h` v0
 *           vocabulary has no `CAP_ERR_REVOKED` — cap_handle.h
 *           collapses the revoked + reaped cases onto
 *           `CAP_ERR_MISSING`. Slice 002's
 *           `broker_svc_cascade_revokes_minted_handle_test.c`
 *           documents the same translation.)
 *        b. `ipc_send_h(recipient_h, fs_read_port, msg)` returns
 *           `IPC_ERR_CAP_DENIED` and emits the canonical
 *           `CAP:DENY:<recipient>:fs_read:-` marker from the
 *           cap_deny_marker formatter (#211 / #221 / #265).
 *
 * Sub-markers emitted (consumed by
 * build/scripts/test_m5_owner_delete_cascade_allow_qemu.sh):
 *
 *   TEST:PASS:m5_owner_delete_cascade_allow_qemu:subtree_revoked_before_destroy_qemu
 *   TEST:PASS:m5_owner_delete_cascade_allow_qemu:delegated_caps_invalid
 *   TEST:SKIP:m5_owner_delete_cascade_allow_qemu:audit_cascade_recorded
 *   TEST:SKIP:m5_owner_delete_cascade_allow_qemu:audit_cascade_done_recorded
 *   TEST:PASS:m5_owner_delete_cascade_allow_qemu
 *
 * The two SKIP markers are intentional — gated on #98 (the cascade
 * audit-event class). They follow the same shape #304 used for the
 * `audit_deny_recorded_qemu` SKIP.
 *
 * Issue: #325. Plan: plans/2026-05-25-m5-ownership-on-m1-substrate.md
 * slice 3.
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
#include "../kernel/svc/fs_svc.h"
#include "../kernel/user/helloapp.h"
#include "../kernel/user/launcher.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:m5_owner_delete_cascade_allow_qemu:%s\n", reason);
  g_fail = 1;
}

static void reset_world(void) {
  launcher_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  proc_sched_reset();
  broker_svc_reset();
  fs_svc_reset();
  cap_broker_reset();
  ipc_port_table_reset();
  launcher_spawn_reset();
}

/* Mint a recv handle for the broker-svc subject so this driver can
 * drain the broker port. Mirrors the helper in m4_broker_share_*. */
static cap_handle_t broker_svc_setup_recv(void) {
  if (cap_grant_for_tests((cap_subject_id_t)SUBJECT_M4_BROKER_SVC,
                          CAP_IPC_SEND) != CAP_OK) {
    return CAP_HANDLE_NULL;
  }
  return cap_handle_grant((cap_subject_id_t)SUBJECT_M4_BROKER_SVC,
                          CAP_IPC_SEND);
}

/* Decode a little-endian 32-bit value from `p`. */
static uint32_t ld_le32(const uint8_t *p) {
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

/* Drain BROKER_OP_REQUEST, fan into cap_broker_request_share. */
static int driver_handle_request(ipc_port_t broker_port,
                                 cap_handle_t recv_h,
                                 cap_subject_id_t expected_owner,
                                 cap_subject_id_t *out_recip,
                                 cap_share_id_t *out_sid) {
  ipc_msg_v0 rx = {0};
  ipc_result_t rr = ipc_recv_h(recv_h, broker_port, &rx);
  if (rr != IPC_OK) {
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
  if (rlen == 0u || rlen > 31u) {
    fail("request_drain_bad_resource_len");
    return 0;
  }
  char resource[32] = {0};
  memcpy(resource, &rx.payload[9], rlen);

  cap_broker_result_t br =
      cap_broker_request_share(expected_owner, recip, cap, resource, out_sid);
  if (br != CAP_BROKER_OK || *out_sid == CAP_SHARE_ID_INVALID) {
    fail("cap_broker_request_share_failed");
    return 0;
  }
  *out_recip = recip;
  return 1;
}

/* Drain BROKER_OP_APPROVE and mint the recipient-side delegated
 * handle via broker_svc_approve_h (slice 002, parented on the
 * owner's broker-port handle so the cascade walker can sweep it). */
static int driver_handle_approve_h(ipc_port_t broker_port,
                                   cap_handle_t recv_h,
                                   cap_subject_id_t expected_owner,
                                   cap_subject_id_t recipient,
                                   cap_share_id_t expected_sid,
                                   cap_handle_t owner_broker_h,
                                   cap_handle_t *out_recipient_h) {
  ipc_msg_v0 rx = {0};
  ipc_result_t rr = ipc_recv_h(recv_h, broker_port, &rx);
  if (rr != IPC_OK) {
    fail("approve_drain_not_ok");
    return 0;
  }
  if (rx.sender_subject != (uint32_t)expected_owner ||
      rx.tag != (uint32_t)BROKER_OP_APPROVE ||
      rx.payload_len < 4u) {
    fail("approve_drain_bad_envelope");
    return 0;
  }
  cap_share_id_t sid = (cap_share_id_t)ld_le32(&rx.payload[0]);
  if (sid != expected_sid) {
    fail("approve_drain_share_id_mismatch");
    return 0;
  }
  cap_broker_result_t br =
      broker_svc_approve_h(expected_owner, sid, recipient, CAP_FS_READ,
                           owner_broker_h, out_recipient_h);
  if (br != CAP_BROKER_OK || *out_recipient_h == CAP_HANDLE_NULL) {
    fail("broker_svc_approve_h_failed");
    return 0;
  }
  return 1;
}

int main(void) {
  reset_world();

  /* Bring up broker_svc + fs_svc (the delegated handle's target). */
  if (broker_svc_init() != BROKER_SVC_OK) {
    fail("broker_svc_init_failed");
    goto out;
  }
  ipc_port_t broker_port = broker_svc_port();
  if (broker_port == IPC_PORT_INVALID) {
    fail("broker_port_invalid");
    goto out;
  }
  if (fs_svc_init() != FS_SVC_OK) {
    fail("fs_svc_init_failed");
    goto out;
  }
  ipc_port_t fs_read_port = fs_svc_port_read();
  if (fs_read_port == IPC_PORT_INVALID) {
    fail("fs_read_port_invalid");
    goto out;
  }

  cap_handle_t broker_recv = broker_svc_setup_recv();
  if (broker_recv == CAP_HANDLE_NULL) {
    fail("broker_svc_recv_setup_failed");
    goto out;
  }

  /* Slice 2 spawn: owner = subject 3 with broker handle stamped into
   * ipc_scratch[24..32). Recipient is a distinct unrelated subject id
   * (issue #325 names "subject=4" but that collides with
   * SUBJECT_M3_FS_SVC which is also the fs_read port owner; using 7
   * keeps the deny marker subject id orthogonal to the service
   * subjects and matches slice 002's cascade test). */
  const cap_subject_id_t owner     = (cap_subject_id_t)SUBJECT_M2_HELLOAPP;
  const cap_subject_id_t recipient = (cap_subject_id_t)7u;
  launcher_manifest_t m = {
      .subject_id       = owner,
      .auto_grant_caps  = NULL,
      .auto_grant_count = 0u,
  };
  launcher_broker_spawn_t sp;
  if (launcher_broker_spawn_app_with_broker_cap(&m, &sp) != LAUNCHER_OK) {
    fail("launcher_broker_spawn_failed");
    goto out;
  }
  if (sp.pid == PID_INVALID || !process_is_live_for_tests(sp.pid) ||
      sp.broker_handle == CAP_HANDLE_NULL) {
    fail("spawned_pcb_bad");
    goto out;
  }
  if (cap_gate_check_handle(sp.broker_handle, CAP_IPC_SEND) != 1) {
    fail("spawned_broker_handle_gate_check");
    goto out;
  }

  /* Owner must hold the cap it's offering (cap_broker precondition). */
  if (cap_table_grant(owner, CAP_FS_READ) != CAP_OK) {
    fail("owner_grant_setup_failed");
    goto out;
  }

  /* Leg 1: BROKER_OP_REQUEST through ipc_send_h. */
  helloapp_broker_owner_result_t ownr = {0};
  helloapp_entry_broker_owner(sp.aspace, broker_port,
                              recipient, CAP_FS_READ,
                              "doc-alpha", 9u,
                              /*share_id_in=*/NULL,
                              &ownr);
  if (ownr.request_send_result != IPC_OK) {
    fail("owner_request_send_not_ok");
    goto out;
  }
  cap_subject_id_t drained_recip = 0u;
  cap_share_id_t   sid           = CAP_SHARE_ID_INVALID;
  if (!driver_handle_request(broker_port, broker_recv, owner,
                             &drained_recip, &sid)) {
    goto out;
  }
  if (drained_recip != recipient) {
    fail("drained_recipient_mismatch");
    goto out;
  }

  /* Leg 2: BROKER_OP_APPROVE → broker_svc_approve_h (parented mint). */
  if (helloapp_entry_broker_owner_approve(sp.aspace, broker_port, sid)
        != IPC_OK) {
    fail("owner_approve_send_not_ok");
    goto out;
  }
  cap_handle_t recipient_h = CAP_HANDLE_NULL;
  if (!driver_handle_approve_h(broker_port, broker_recv, owner,
                               recipient, sid, sp.broker_handle,
                               &recipient_h)) {
    goto out;
  }

  /* Pre-cascade sanity: both handles resolve. */
  if (cap_gate_check_handle(recipient_h, CAP_FS_READ) != 1) {
    fail("recipient_handle_pre_gate");
    goto out;
  }
  if (cap_gate_check_handle(sp.broker_handle, CAP_IPC_SEND) != 1) {
    fail("owner_broker_handle_pre_gate");
    goto out;
  }

  /* Run the cascade as a self-delete with PCB teardown SUPPRESSED
   * (PID_INVALID). This is the load-bearing call for the
   * subtree_revoked_before_destroy_qemu sub-check: step 4 of
   * broker_svc_delete_owner is skipped, so any subsequent
   * cap_gate_check_handle_result == CAP_ERR_MISSING result proves
   * the kill came from step 3 (cap_handle_revoke_subtree). */
  uint32_t n_children = 0u;
  broker_svc_result_t dr =
      broker_svc_delete_owner(owner, owner, sp.broker_handle,
                              PID_INVALID, &n_children);
  if (dr != BROKER_SVC_OK) {
    fail("broker_svc_delete_owner_not_ok");
    goto out;
  }
  if (n_children == 0u) {
    fail("cascade_count_zero");
    goto out;
  }
  /* Owner PCB must still be live — process_destroy was suppressed. */
  if (!process_is_live_for_tests(sp.pid)) {
    fail("owner_pcb_unexpectedly_destroyed");
    goto out;
  }

  /* subtree_revoked_before_destroy_qemu: recipient handle is stale
   * via the subtree walker, not via process_destroy's subject-bulk
   * revoke (which never ran here). CAP_ERR_MISSING is the v0
   * spelling — see file-header note. */
  cap_result_t rcheck =
      cap_gate_check_handle_result(recipient_h, CAP_FS_READ);
  if (rcheck != CAP_ERR_MISSING) {
    fprintf(stderr,
            "post-cascade recipient gate_check_result = %d (want %d)\n",
            (int)rcheck, (int)CAP_ERR_MISSING);
    fail("recipient_handle_post_not_stale_before_destroy");
    goto out;
  }
  printf("TEST:PASS:m5_owner_delete_cascade_allow_qemu:"
         "subtree_revoked_before_destroy_qemu\n");

  /* delegated_caps_invalid: the revoked delegated handle must fail
   * the real ipc_send_h gate. We construct a minimal in-bounds
   * envelope on the stack — recipient has no PCB, so the IPC
   * aspace bounds check is a no-op (kernel/ipc/ipc_ops.c
   * §ipc_aspace_bounds_check carve-out), and the cap gate is the
   * dominant decision. */
  ipc_msg_v0 probe = {0};
  probe.abi_version = (uint16_t)OS_ABI_VERSION;
  probe.flags = 0u;
  probe.tag = 0u;
  probe.payload_len = 0u;
  ipc_result_t sr = ipc_send_h(recipient_h, fs_read_port, &probe);
  if (sr != IPC_ERR_CAP_DENIED) {
    fprintf(stderr,
            "post-cascade ipc_send_h = %d (want IPC_ERR_CAP_DENIED=%d)\n",
            (int)sr, (int)IPC_ERR_CAP_DENIED);
    fail("delegated_send_should_have_been_denied");
    goto out;
  }
  printf("TEST:PASS:m5_owner_delete_cascade_allow_qemu:"
         "delegated_caps_invalid\n");

  /* Audit SKIPs — gated on #98 (cap.revoked.cascade + cap.cascade.done
   * event classes). Mirror the #304 audit_deny_recorded_qemu shape. */
  printf("TEST:SKIP:m5_owner_delete_cascade_allow_qemu:"
         "audit_cascade_recorded\n");
  printf("TEST:SKIP:m5_owner_delete_cascade_allow_qemu:"
         "audit_cascade_done_recorded\n");

  /* Cleanup. Owner PCB is still live because we passed PID_INVALID
   * above; tear it down through the launcher so the spawn-table
   * accounting stays clean. */
  if (launcher_broker_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("broker_spawn_destroy_failed");
    goto out;
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:m5_owner_delete_cascade_allow_qemu\n");
  return 0;
}
