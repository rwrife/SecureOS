/**
 * @file m5_owner_delete_cascade_deny_qemu_test.c
 * @brief M5-on-M1 substrate peer — `BROKER_OP_DELETE_OWNER` deny-path
 *        (slice 4 of plan #319, issue #326).
 *
 * Companion to the slice-3 allow-path peer
 * (`m5_owner_delete_cascade_allow_qemu_test.c`). Rides on the merged
 * M1/M2/M3/M4 substrate plus the slice-001/002/003 cascade plumbing
 * and asserts three deny-side invariants:
 *
 *   1. `bystander_cannot_delete_owner`
 *      A third subject (not owner, not SUBJECT_M5_ADMIN) issuing
 *      `broker_svc_delete_owner` is rejected with
 *      `BROKER_SVC_ERR_DELETE_DENIED`; the predicate emits the
 *      canonical `CAP:DENY:<actor>:capability_admin:delete_owner_<owner>`
 *      marker via the cap_deny_marker formatter (#211/#221/#265);
 *      every previously minted handle (owner broker handle +
 *      delegated child) still resolves on `cap_gate_check_handle`.
 *      The shell driver greps for the deny-marker string explicitly.
 *
 *   2. `double_delete_is_idempotent`
 *      An owner-issued cascade (allow leg) succeeds and revokes the
 *      delegated handle. A second invocation against the same owner
 *      returns `BROKER_SVC_OK` again (the authority check still
 *      allows self-delete; the subtree walker's root is now stale
 *      and its `CAP_ERR_MISSING` return is intentionally discarded
 *      by `broker_svc_delete_owner` per its file-header contract,
 *      step 3). The previously minted recipient handle continues to
 *      gate as `CAP_ERR_MISSING`.
 *
 *      (Issue body wrote `CAP_BROKER_OK`; broker_svc_delete_owner is
 *      a `broker_svc_result_t` API so the equivalent v0 spelling is
 *      `BROKER_SVC_OK`. Same translation slice 003 used for
 *      `CAP_ERR_REVOKED` -> `CAP_ERR_MISSING`.)
 *
 *   3. `process_destroy_recycle_revokes_qemu`
 *      Tear down the owner PCB via `launcher_broker_spawn_destroy`
 *      (which calls `process_destroy`, which bulk-revokes the
 *      subject's cap handles per #240). Then `process_create` a
 *      fresh PCB for the SAME subject id — i.e. the subject id is
 *      recycled. Old (pre-cascade) handles continue to deny with
 *      `CAP_ERR_MISSING` rather than silently succeeding against
 *      the recycled subject, because their slot's generation was
 *      bumped during the cascade and remains so for the recycled
 *      tenant.
 *
 * Sub-markers (consumed by
 * build/scripts/test_m5_owner_delete_cascade_deny_qemu.sh):
 *
 *   TEST:PASS:m5_owner_delete_cascade_deny_qemu:bystander_cannot_delete_owner
 *   TEST:PASS:m5_owner_delete_cascade_deny_qemu:double_delete_is_idempotent
 *   TEST:PASS:m5_owner_delete_cascade_deny_qemu:process_destroy_recycle_revokes_qemu
 *   TEST:PASS:m5_owner_delete_cascade_deny_qemu
 *
 * Issue: #326. Plan: plans/2026-05-25-m5-ownership-on-m1-substrate.md
 * slice 4. Mirrors #271 / #281 / #305 in shape.
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
  printf("TEST:FAIL:m5_owner_delete_cascade_deny_qemu:%s\n", reason);
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
 * drain the broker port. Mirrors the helper used by the allow peer. */
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

/* Drive owner -> broker request leg and translate to
 * cap_broker_request_share, matching the allow peer's driver. */
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

/* Drive owner -> broker approve leg, minting the recipient handle
 * via broker_svc_approve_h so it's parented on the owner's broker
 * handle (i.e. inside the subtree the cascade walks). */
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

/* Run the owner -> broker request + approve dance and produce a
 * parented recipient handle. Returns 1 on success. */
static int mint_delegated_recipient(cap_subject_id_t owner,
                                    cap_subject_id_t recipient,
                                    launcher_broker_spawn_t *sp,
                                    cap_handle_t broker_recv,
                                    ipc_port_t broker_port,
                                    cap_handle_t *out_recipient_h) {
  helloapp_broker_owner_result_t ownr = {0};
  helloapp_entry_broker_owner(sp->aspace, broker_port,
                              recipient, CAP_FS_READ,
                              "doc-deny", 8u,
                              /*share_id_in=*/NULL,
                              &ownr);
  if (ownr.request_send_result != IPC_OK) {
    fail("owner_request_send_not_ok");
    return 0;
  }
  cap_subject_id_t drained_recip = 0u;
  cap_share_id_t   sid           = CAP_SHARE_ID_INVALID;
  if (!driver_handle_request(broker_port, broker_recv, owner,
                             &drained_recip, &sid)) {
    return 0;
  }
  if (drained_recip != recipient) {
    fail("drained_recipient_mismatch");
    return 0;
  }
  if (helloapp_entry_broker_owner_approve(sp->aspace, broker_port, sid)
        != IPC_OK) {
    fail("owner_approve_send_not_ok");
    return 0;
  }
  if (!driver_handle_approve_h(broker_port, broker_recv, owner,
                               recipient, sid, sp->broker_handle,
                               out_recipient_h)) {
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
  if (fs_svc_init() != FS_SVC_OK) {
    fail("fs_svc_init_failed");
    goto out;
  }

  cap_handle_t broker_recv = broker_svc_setup_recv();
  if (broker_recv == CAP_HANDLE_NULL) {
    fail("broker_svc_recv_setup_failed");
    goto out;
  }

  /* owner = subject 3 (HelloApp); bystander = subject 7 (distinct
   * from owner and SUBJECT_M5_ADMIN=6; still within v0
   * CAP_TABLE_MAX_SUBJECTS=8). */
  const cap_subject_id_t owner     = (cap_subject_id_t)SUBJECT_M2_HELLOAPP;
  const cap_subject_id_t recipient = (cap_subject_id_t)7u;
  const cap_subject_id_t bystander = (cap_subject_id_t)7u;

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
  if (cap_table_grant(owner, CAP_FS_READ) != CAP_OK) {
    fail("owner_grant_setup_failed");
    goto out;
  }

  cap_handle_t recipient_h = CAP_HANDLE_NULL;
  if (!mint_delegated_recipient(owner, recipient, &sp, broker_recv,
                                broker_port, &recipient_h)) {
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

  /* ------------------------------------------------------------------
   * Sub-check 1: bystander_cannot_delete_owner
   * ------------------------------------------------------------------ */
  uint32_t n_children = 99u;  /* sentinel - must be cleared to 0 */
  broker_svc_result_t br_bystander =
      broker_svc_delete_owner(bystander, owner, sp.broker_handle,
                              PID_INVALID, &n_children);
  if (br_bystander != BROKER_SVC_ERR_DELETE_DENIED) {
    fprintf(stderr,
            "bystander broker_svc_delete_owner = %d (want %d)\n",
            (int)br_bystander, (int)BROKER_SVC_ERR_DELETE_DENIED);
    fail("bystander_not_denied");
    goto out;
  }
  if (n_children != 0u) {
    fail("bystander_deny_leaked_children_count");
    goto out;
  }
  /* Authority denial must not collaterally revoke anything. */
  if (cap_gate_check_handle(sp.broker_handle, CAP_IPC_SEND) != 1) {
    fail("owner_broker_handle_should_survive_deny");
    goto out;
  }
  if (cap_gate_check_handle(recipient_h, CAP_FS_READ) != 1) {
    fail("recipient_handle_should_survive_deny");
    goto out;
  }
  if (!process_is_live_for_tests(sp.pid)) {
    fail("owner_pcb_should_survive_deny");
    goto out;
  }
  /* The shell driver greps for the exact deny marker
   * (`CAP:DENY:7:capability_admin:delete_owner_3\n`) over the test's
   * stdout — emitted by cap_broker_delete_owner_check. */
  printf("TEST:PASS:m5_owner_delete_cascade_deny_qemu:"
         "bystander_cannot_delete_owner\n");

  /* Issue #370 audit assertion (bystander path):
   *   - exactly one cap.deny event recorded against the bystander
   *     for {actor=bystander, subject=owner, cap=CAP_CAPABILITY_ADMIN}
   *   - zero cap.cascade.* events emitted on the deny path (no
   *     false-positive cascade audit). */
  {
    size_t total = cap_audit_count_for_tests();
    int deny_count       = 0;
    int cascade_seen     = 0;
    int cascade_done_seen = 0;
    for (size_t i = 0u; i < total; ++i) {
      cap_audit_event_t ev;
      if (cap_audit_get_for_tests(i, &ev) != CAP_OK) continue;
      if (ev.operation == CAP_AUDIT_OP_CHECK &&
          ev.actor_subject_id == bystander &&
          ev.subject_id == owner &&
          ev.capability_id == CAP_CAPABILITY_ADMIN &&
          ev.result == CAP_ERR_MISSING) {
        deny_count++;
      }
      if (ev.operation == CAP_AUDIT_OP_CASCADE_REVOKE) {
        cascade_seen = 1;
      }
      if (ev.operation == CAP_AUDIT_OP_CASCADE_DONE) {
        cascade_done_seen = 1;
      }
    }
    if (deny_count != 1) {
      fprintf(stderr, "bystander deny audit count = %d (want 1)\n", deny_count);
      fail("bystander_deny_audit_not_recorded");
      goto out;
    }
    if (cascade_seen || cascade_done_seen) {
      fail("bystander_deny_leaked_cascade_audit");
      goto out;
    }
  }
  printf("TEST:PASS:m5_owner_delete_cascade_deny_qemu:"
         "audit_deny_recorded_no_cascade_qemu\n");

  /* ------------------------------------------------------------------
   * Sub-check 2: double_delete_is_idempotent
   *
   * First call (owner self-delete, PCB teardown suppressed) succeeds
   * and revokes the delegated subtree. Second call against the same
   * (now-stale) root must still return BROKER_SVC_OK because
   * broker_svc_delete_owner discards the subtree walker's stale-root
   * return value (see kernel/svc/broker_svc.c step 3 `(void)` cast).
   * ------------------------------------------------------------------ */
  uint32_t first_n = 0u;
  broker_svc_result_t br_first =
      broker_svc_delete_owner(owner, owner, sp.broker_handle,
                              PID_INVALID, &first_n);
  if (br_first != BROKER_SVC_OK) {
    fprintf(stderr,
            "first owner-cascade = %d (want BROKER_SVC_OK=%d)\n",
            (int)br_first, (int)BROKER_SVC_OK);
    fail("first_cascade_not_ok");
    goto out;
  }
  if (first_n == 0u) {
    fail("first_cascade_count_zero");
    goto out;
  }
  /* Recipient handle is now stale via the subtree walker. */
  if (cap_gate_check_handle_result(recipient_h, CAP_FS_READ)
        != CAP_ERR_MISSING) {
    fail("recipient_should_be_revoked_after_first_cascade");
    goto out;
  }

  /* Second call: still allowed (self-delete authority), still
   * BROKER_SVC_OK, but n_children must be 0 — the side-table was
   * swept clean by the first pass, and the subtree walker's stale
   * root is harmlessly absorbed. */
  uint32_t second_n = 99u;
  broker_svc_result_t br_second =
      broker_svc_delete_owner(owner, owner, sp.broker_handle,
                              PID_INVALID, &second_n);
  if (br_second != BROKER_SVC_OK) {
    fprintf(stderr,
            "second owner-cascade = %d (want BROKER_SVC_OK=%d)\n",
            (int)br_second, (int)BROKER_SVC_OK);
    fail("second_cascade_not_idempotent");
    goto out;
  }
  if (second_n != 0u) {
    fail("second_cascade_should_have_swept_zero");
    goto out;
  }
  /* And the recipient handle remains revoked. */
  if (cap_gate_check_handle_result(recipient_h, CAP_FS_READ)
        != CAP_ERR_MISSING) {
    fail("recipient_should_still_be_revoked_after_second_cascade");
    goto out;
  }
  printf("TEST:PASS:m5_owner_delete_cascade_deny_qemu:"
         "double_delete_is_idempotent\n");

  /* ------------------------------------------------------------------
   * Sub-check 3: process_destroy_recycle_revokes_qemu
   *
   * Tear down the owner PCB through the launcher (process_destroy
   * inside bulk-revokes the subject's handles via
   * cap_handle_revoke_subject per #240). Then process_create a fresh
   * PCB for the SAME subject id and assert old recipient_h does NOT
   * resurrect — its slot's generation was bumped by the cascade and
   * stays bumped across the recycle.
   * ------------------------------------------------------------------ */
  if (launcher_broker_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("broker_spawn_destroy_failed");
    goto out;
  }
  if (process_is_live_for_tests(sp.pid)) {
    fail("owner_pid_should_be_dead_after_destroy");
    goto out;
  }

  process_id_t recycled_pid = PID_INVALID;
  if (process_create(owner, NULL, &recycled_pid) != PROC_OK) {
    fail("process_create_recycle_failed");
    goto out;
  }
  if (recycled_pid == PID_INVALID || !process_is_live_for_tests(recycled_pid)) {
    fail("recycled_pcb_not_live");
    goto out;
  }

  /* The old delegated handle MUST still deny — recycling the
   * subject id must not silently re-grant rows that the cascade
   * already revoked. */
  if (cap_gate_check_handle_result(recipient_h, CAP_FS_READ)
        != CAP_ERR_MISSING) {
    fail("recipient_resurrected_after_subject_recycle");
    goto out;
  }
  /* And the (revoked) owner broker handle stays dead too — the
   * recycled PCB does not get free access to the prior tenant's
   * broker port. */
  if (cap_gate_check_handle_result(sp.broker_handle, CAP_IPC_SEND)
        != CAP_ERR_MISSING) {
    fail("owner_broker_handle_resurrected_after_subject_recycle");
    goto out;
  }
  printf("TEST:PASS:m5_owner_delete_cascade_deny_qemu:"
         "process_destroy_recycle_revokes_qemu\n");

  /* Cleanup the recycled PCB so the test exits with a clean table. */
  (void)process_destroy(recycled_pid);

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:m5_owner_delete_cascade_deny_qemu\n");
  return 0;
}
