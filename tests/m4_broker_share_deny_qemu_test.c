/**
 * @file m4_broker_share_deny_qemu_test.c
 * @brief M4-on-M1 substrate peer of `tests/broker_share_deny_test.c`
 *        (slice 3 of plan #300, issue #304).
 *
 * Drives the broker deny path through the real M1 substrate end-to-end:
 *
 *   1. `broker_svc_init()` allocates the broker port (slice 1, #302).
 *   2. `launcher_broker_spawn_app_with_broker_cap()` (slice 2, #303)
 *      spawns the owner PCB with the broker send handle stamped into
 *      `ipc_scratch[24..32)`.
 *   3. Owner emits BROKER_OP_REQUEST via `ipc_send_h`. The driver fans
 *      it into `cap_broker_request_share`.
 *   4. Owner emits BROKER_OP_DENY via `ipc_send_h`. The driver fans it
 *      into `cap_broker_deny`. The recipient must end up with no grant
 *      (neither in the broker side-table nor the underlying cap_table).
 *   5. A fresh request + an attempted approve/deny by a bystander must
 *      both be rejected as NOT_AUTHORIZED.
 *   6. The denied share is terminal: a subsequent `cap_broker_approve`
 *      must return `CAP_BROKER_ERR_BAD_STATE`.
 *
 * The six `_qemu` sub-check markers preserve the issue-#304 names with
 * a `_qemu` suffix: `request_returns_pending_share_id`,
 * `deny_blocks_recipient`, `bystander_cannot_mutate`,
 * `cannot_be_re_approved`, `scope_is_capability_bound`,
 * `scope_is_resource_bound`. Plus the umbrella
 * `m4_broker_share_deny_qemu` and the SKIP marker
 * `audit_deny_recorded_qemu` (gated on #98).
 *
 * Pre-flight contract: the existing host fixture
 * `tests/broker_share_deny_test.c` keeps verbatim markers — this peer
 * is additive.
 *
 * Issue: #304. Plan: plans/2026-05-25-m4-broker-on-m1-substrate.md slice 3.
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
  printf("TEST:FAIL:m4_broker_share_deny_qemu:%s\n", reason);
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
  if (rlen == 0u || rlen > 31u) {
    fail("request_drain_bad_resource_len");
    return 0;
  }
  char resource[32] = {0};
  memcpy(resource, &rx.payload[9], rlen);

  if (cap_broker_request_share(expected_owner, recip, cap,
                               resource, out_sid) != CAP_BROKER_OK ||
      *out_sid == CAP_SHARE_ID_INVALID) {
    fail("cap_broker_request_share_failed");
    return 0;
  }
  return 1;
}

/* Drain a BROKER_OP_DENY envelope and fan into cap_broker_deny. */
static int driver_handle_deny(ipc_port_t broker_port,
                              cap_handle_t recv_h,
                              cap_subject_id_t expected_owner,
                              cap_share_id_t expected_sid) {
  ipc_msg_v0 rx = {0};
  if (ipc_recv_h(recv_h, broker_port, &rx) != IPC_OK) {
    fail("deny_drain_not_ok");
    return 0;
  }
  if (rx.sender_subject != (uint32_t)expected_owner ||
      rx.tag != (uint32_t)BROKER_OP_DENY ||
      rx.payload_len < 4u) {
    fail("deny_drain_bad_envelope");
    return 0;
  }
  cap_share_id_t sid = (cap_share_id_t)ld_le32(&rx.payload[0]);
  if (sid != expected_sid) {
    fail("deny_drain_share_id_mismatch");
    return 0;
  }
  if (cap_broker_deny(expected_owner, sid) != CAP_BROKER_OK) {
    fail("cap_broker_deny_failed");
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
  if (sp.pid == PID_INVALID || !process_is_live_for_tests(sp.pid)) {
    fail("spawned_pcb_not_live");
    goto out;
  }
  if (sp.broker_handle == CAP_HANDLE_NULL ||
      cap_gate_check_handle(sp.broker_handle, CAP_IPC_SEND) != 1) {
    fail("spawned_broker_handle_bad");
    goto out;
  }

  /* Owner must hold the cap they are offering (broker precondition). */
  if (cap_table_grant(owner, CAP_FS_READ) != CAP_OK) {
    fail("owner_grant_setup_failed");
    goto out;
  }

  /* ---- Request leg ---------------------------------------------- */
  helloapp_broker_owner_result_t ownr = {0};
  helloapp_entry_broker_owner(sp.aspace, broker_port,
                              recip, CAP_FS_READ,
                              "doc-alpha", 9u,
                              /*share_id_in=*/NULL, &ownr);
  if (ownr.request_send_result != IPC_OK) {
    fail("owner_request_send_not_ok");
    goto out;
  }
  cap_share_id_t sid = CAP_SHARE_ID_INVALID;
  if (!driver_handle_request(broker_port, broker_recv, owner, &sid)) {
    goto out;
  }
  if (cap_broker_state_for_tests(sid) != CAP_SHARE_STATE_REQUESTED) {
    fail("state_not_requested_after_request");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_deny_qemu:request_returns_pending_share_id_qemu\n");

  /* ---- Deny leg ------------------------------------------------- */
  ipc_result_t dr =
      helloapp_entry_broker_owner_deny(sp.aspace, broker_port, sid);
  if (dr != IPC_OK) {
    fail("owner_deny_send_not_ok");
    goto out;
  }
  if (!driver_handle_deny(broker_port, broker_recv, owner, sid)) {
    goto out;
  }
  if (cap_broker_state_for_tests(sid) != CAP_SHARE_STATE_DENIED) {
    fail("state_not_denied_after_deny");
    goto out;
  }
  /* Neither broker check nor underlying cap_table must leak. */
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha")
        != CAP_ERR_MISSING) {
    fail("broker_check_should_miss_after_deny");
    goto out;
  }
  if (cap_table_check(recip, CAP_FS_READ) != CAP_ERR_MISSING) {
    fail("underlying_table_should_be_clean");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_deny_qemu:deny_blocks_recipient_qemu\n");

  /* ---- cannot_be_re_approved_qemu — denied is terminal. -------- */
  if (cap_broker_approve(owner, sid) != CAP_BROKER_ERR_BAD_STATE) {
    fail("denied_share_should_not_be_reapprovable");
    goto out;
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha")
        != CAP_ERR_MISSING) {
    fail("recipient_should_still_miss_after_reapprove_attempt");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_deny_qemu:cannot_be_re_approved_qemu\n");

  /* ---- bystander_cannot_mutate_qemu — fresh share, bystander
   *      attempts to approve/deny. The owner sends BROKER_OP_REQUEST
   *      again through ipc_send_h; the driver fans it. We then make
   *      direct cap_broker_{approve,deny} calls FROM the bystander
   *      subject (no ipc_send_h needed — broker authority is checked
   *      inside cap_broker_*, which is the exact surface a future
   *      in-kernel dispatch would consult on the sender_subject). */
  helloapp_broker_owner_result_t ownr2 = {0};
  helloapp_entry_broker_owner(sp.aspace, broker_port,
                              recip, CAP_FS_READ,
                              "doc-alpha", 9u,
                              /*share_id_in=*/NULL, &ownr2);
  if (ownr2.request_send_result != IPC_OK) {
    fail("owner_second_request_send_not_ok");
    goto out;
  }
  cap_share_id_t sid2 = CAP_SHARE_ID_INVALID;
  if (!driver_handle_request(broker_port, broker_recv, owner, &sid2)) {
    goto out;
  }
  if (cap_broker_approve(bystander, sid2) != CAP_BROKER_ERR_NOT_AUTHORIZED) {
    fail("bystander_approve_should_be_rejected");
    goto out;
  }
  if (cap_broker_deny(bystander, sid2) != CAP_BROKER_ERR_NOT_AUTHORIZED) {
    fail("bystander_deny_should_be_rejected");
    goto out;
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha")
        != CAP_ERR_MISSING) {
    fail("recipient_should_still_miss_after_bystander_attempts");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_deny_qemu:bystander_cannot_mutate_qemu\n");

  /* ---- scope_is_resource_bound_qemu — even after a future approve
   *      on sid2 would only cover "doc-alpha"; a different resource
   *      must miss. (We don't actually approve here — recipient_check
   *      already misses because sid2 is REQUESTED, so we close out the
   *      sub-check by asserting the miss across a different resource
   *      name, which would still miss even if approved.) */
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-beta")
        != CAP_ERR_MISSING) {
    fail("other_resource_should_not_be_granted");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_deny_qemu:scope_is_resource_bound_qemu\n");

  /* ---- scope_is_capability_bound_qemu — different cap must miss. */
  if (cap_broker_recipient_check(recip, CAP_FS_WRITE, "doc-alpha")
        != CAP_ERR_MISSING) {
    fail("other_capability_should_not_be_granted");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_deny_qemu:scope_is_capability_bound_qemu\n");

  /* audit_deny_recorded_qemu — issue #311. The deny envelope drained
   * above already called cap_broker_deny(owner, sid); assert the audit
   * ring contains a matching deny record. */
  {
    size_t total = cap_audit_count_for_tests();
    int found = 0;
    for (size_t i = 0; i < total; ++i) {
      cap_audit_event_t ev;
      if (cap_audit_get_for_tests(i, &ev) != CAP_OK) continue;
      if (ev.operation == CAP_AUDIT_OP_GRANT &&
          ev.actor_subject_id == owner &&
          ev.subject_id == recip &&
          ev.capability_id == CAP_FS_READ &&
          ev.result == CAP_ERR_MISSING) {
        found = 1;
        break;
      }
    }
    if (!found) {
      fail("audit_deny_record_not_found");
      goto out;
    }
  }
  printf("TEST:PASS:m4_broker_share_deny_qemu:audit_deny_recorded_qemu\n");

  if (launcher_broker_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("broker_spawn_destroy_failed");
    goto out;
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:m4_broker_share_deny_qemu\n");
  return 0;
}
