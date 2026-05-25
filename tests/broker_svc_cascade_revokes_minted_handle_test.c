/**
 * @file broker_svc_cascade_revokes_minted_handle_test.c
 * @brief Validator for the BROKER_OP_DELETE_OWNER cascade behaviour
 *        (M5-SUBSTRATE-002, issue #324).
 *
 * Asserts the contract documented in `kernel/svc/broker_svc.h`:
 *
 *   1. `broker_svc_approve_h` mints a recipient-side cap_handle row
 *      that is parented on the owner's broker-port handle (so the
 *      slice-001 subtree walker can later sweep it).
 *   2. The minted recipient handle initially passes
 *      `cap_gate_check_handle` for the granted cap.
 *   3. After `broker_svc_delete_owner(owner, owner, broker_port_h, ...)`,
 *      the recipient handle now fails `cap_gate_check_handle_result`
 *      with `CAP_ERR_MISSING` (the slice-001 walker bumped the row's
 *      generation; cap_handle.h reuses CAP_ERR_MISSING for stale rows
 *      \u2014 there is no CAP_ERR_REVOKED in the v0 vocabulary, see
 *      `kernel/cap/capability.h`).
 *   4. The cascade count surfaced via `out_n` equals 1 (one minted
 *      child handle was bookkept).
 *   5. The owner's broker-port handle itself also fails the gate
 *      check post-cascade (the subtree root was revoked).
 *
 * Output markers (consumed by
 * build/scripts/test_broker_svc_cascade_revokes_minted_handle.sh):
 *   TEST:PASS:broker_svc_cascade_pre_revoke_recipient_gate
 *   TEST:PASS:broker_svc_cascade_pre_revoke_owner_gate
 *   TEST:PASS:broker_svc_cascade_delete_owner_ok
 *   TEST:PASS:broker_svc_cascade_post_revoke_recipient_gate
 *   TEST:PASS:broker_svc_cascade_post_revoke_owner_gate
 *   TEST:PASS:broker_svc_cascade_count
 *   TEST:PASS:broker_svc_cascade_revokes_minted_handle
 *
 * Pure host-side, no qemu / no real PCBs. The `process_destroy` step
 * of the cascade is exercised by passing `PID_INVALID` so the test
 * doesn't have to spin up a launcher \u2014 the load-bearing cap-handle
 * revoke is the property being pinned here. The `_qemu`-tier peer
 * (slice 003 / 004, issues #325 / #326) covers end-to-end PCB
 * teardown + slot recycle.
 *
 * Issue: #324. Plan: plans/2026-05-25-m5-ownership-on-m1-substrate.md
 * slice 2.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_broker.h"
#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/proc/process.h"
#include "../kernel/svc/broker_svc.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:broker_svc_cascade_revokes_minted_handle:%s\n", reason);
  g_fail = 1;
}

static void reset_world(void) {
  broker_svc_reset();
  cap_broker_reset();
  cap_handle_table_reset();
  ipc_port_table_reset();
  cap_table_reset();
}

int main(void) {
  reset_world();

  /* Bring up broker_svc. */
  if (broker_svc_init() != BROKER_SVC_OK) {
    fail("broker_svc_init");
    goto out;
  }
  ipc_port_t broker_port = broker_svc_port();
  if (broker_port == IPC_PORT_INVALID) {
    fail("broker_port_invalid");
    goto out;
  }

  /* Subjects: owner offers CAP_FS_READ to recipient on a named
   * resource. The owner must hold the cap (cap_broker_request_share
   * precondition). */
  const cap_subject_id_t owner     = (cap_subject_id_t)SUBJECT_M2_HELLOAPP;
  const cap_subject_id_t recipient = (cap_subject_id_t)7u;

  if (cap_table_grant(owner, CAP_FS_READ) != CAP_OK) {
    fail("owner_grant_setup");
    goto out;
  }

  /* Mint the owner's broker-port handle \u2014 this is the parent edge
   * the cascade walker will key off. Mirrors the launcher's slice-2
   * (#303) ipc_scratch[24..31] handoff. */
  cap_handle_t owner_broker_h = cap_handle_grant(owner, CAP_IPC_SEND);
  if (owner_broker_h == CAP_HANDLE_NULL) {
    fail("owner_broker_handle_mint");
    goto out;
  }
  if (cap_gate_check_handle(owner_broker_h, CAP_IPC_SEND) != 1) {
    fail("owner_broker_handle_gate_setup");
    goto out;
  }

  /* Issue + approve a share through the broker_svc layer. */
  cap_share_id_t sid = CAP_SHARE_ID_INVALID;
  if (cap_broker_request_share(owner, recipient, CAP_FS_READ,
                               "doc-alpha", &sid) != CAP_BROKER_OK) {
    fail("cap_broker_request_share");
    goto out;
  }
  if (sid == CAP_SHARE_ID_INVALID) {
    fail("share_id_invalid_after_request");
    goto out;
  }

  cap_handle_t recipient_h = CAP_HANDLE_NULL;
  cap_broker_result_t ar = broker_svc_approve_h(owner, sid,
                                                recipient, CAP_FS_READ,
                                                owner_broker_h,
                                                &recipient_h);
  if (ar != CAP_BROKER_OK) {
    fail("broker_svc_approve_h_not_ok");
    goto out;
  }
  if (recipient_h == CAP_HANDLE_NULL) {
    fail("recipient_handle_null_after_approve");
    goto out;
  }

  /* Pre-cascade: recipient handle resolves; owner handle resolves. */
  if (cap_gate_check_handle(recipient_h, CAP_FS_READ) != 1) {
    fail("recipient_handle_pre_gate");
    goto out;
  }
  printf("TEST:PASS:broker_svc_cascade_pre_revoke_recipient_gate\n");

  if (cap_gate_check_handle(owner_broker_h, CAP_IPC_SEND) != 1) {
    fail("owner_handle_pre_gate");
    goto out;
  }
  printf("TEST:PASS:broker_svc_cascade_pre_revoke_owner_gate\n");

  /* Run the cascade as a self-delete (actor == owner, no PCB to
   * destroy in this host fixture). */
  uint32_t n_children = 0u;
  broker_svc_result_t dr = broker_svc_delete_owner(owner, owner,
                                                   owner_broker_h,
                                                   PID_INVALID,
                                                   &n_children);
  if (dr != BROKER_SVC_OK) {
    fail("broker_svc_delete_owner_not_ok");
    goto out;
  }
  printf("TEST:PASS:broker_svc_cascade_delete_owner_ok\n");

  /* Post-cascade: recipient handle must now fail gate-check with
   * CAP_ERR_MISSING (stale row, slice-001 semantics). */
  cap_result_t rcheck = cap_gate_check_handle_result(recipient_h, CAP_FS_READ);
  if (rcheck != CAP_ERR_MISSING) {
    fprintf(stderr, "post-cascade recipient gate_check_result = %d (want %d)\n",
            (int)rcheck, (int)CAP_ERR_MISSING);
    fail("recipient_handle_post_not_stale");
    goto out;
  }
  printf("TEST:PASS:broker_svc_cascade_post_revoke_recipient_gate\n");

  /* The owner's broker-port handle is the subtree root and must
   * also be stale after the cascade. */
  cap_result_t ocheck = cap_gate_check_handle_result(owner_broker_h, CAP_IPC_SEND);
  if (ocheck != CAP_ERR_MISSING) {
    fprintf(stderr, "post-cascade owner gate_check_result = %d (want %d)\n",
            (int)ocheck, (int)CAP_ERR_MISSING);
    fail("owner_handle_post_not_stale");
    goto out;
  }
  printf("TEST:PASS:broker_svc_cascade_post_revoke_owner_gate\n");

  /* Side-table cascade count: one minted child handle was bookkept. */
  if (n_children != 1u) {
    fprintf(stderr, "cascade count = %u (want 1)\n", (unsigned)n_children);
    fail("cascade_count_wrong");
    goto out;
  }
  printf("TEST:PASS:broker_svc_cascade_count\n");

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:broker_svc_cascade_revokes_minted_handle\n");
  return 0;
}
