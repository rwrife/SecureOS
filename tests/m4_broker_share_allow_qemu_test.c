/**
 * @file m4_broker_share_allow_qemu_test.c
 * @brief M4-on-M1 substrate peer of `tests/broker_share_allow_test.c`
 *        (slice 3 of plan #300, issue #304).
 *
 * Rides on the merged M1 substrate end-to-end (no QEMU image — the
 * `_qemu` suffix follows the same convention #259/#270/#280 committed
 * to):
 *
 *   1. `broker_svc_init()` allocates the well-known broker port
 *      (slice 1, #302) owned by `SUBJECT_M4_BROKER_SVC` and gated
 *      by `CAP_IPC_SEND` for both send + recv.
 *   2. The broker-svc subject is granted CAP_IPC_SEND on the legacy
 *      bitset and a recv handle is minted, so this test driver can
 *      drain the broker port via `ipc_recv_h` standing in for the
 *      future broker_svc recv loop.
 *   3. `launcher_broker_spawn_app_with_broker_cap()` (slice 2, #303)
 *      produces a live PCB with the broker send handle stamped LE64
 *      into `ipc_scratch[24..32)`.
 *   4. The owner (real spawned PCB) calls `helloapp_entry_broker_owner`
 *      twice — once with `share_id_in == NULL` to send BROKER_OP_REQUEST,
 *      once after we know the broker-assigned share id to send
 *      BROKER_OP_APPROVE.
 *   5. The test driver drains each envelope, parses the on-wire schema
 *      from `kernel/svc/broker_svc.h`, and fans into `cap_broker_*` —
 *      the same pattern slice 3 of M3 used for fs_svc.
 *
 * The five `_qemu` sub-check markers preserve the spelling of the
 * existing host-fixture sub-checks
 * (`owner_holds_cap`, `request_returns_pending_share_id`,
 *  `approve_grants_recipient`, `scope_is_capability_bound`,
 *  `scope_is_resource_bound`) with a `_qemu` suffix, plus the umbrella
 * `m4_broker_share_allow_qemu`. They are consumed by
 * `build/scripts/test_m4_broker_share_allow_qemu.sh`.
 *
 * Pre-flight contract: the existing host fixture
 * `tests/broker_share_allow_test.c` keeps verbatim markers — this
 * peer is additive (see validate_bundle.sh #110 / capability matrix
 * #155 / #236).
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
  printf("TEST:FAIL:m4_broker_share_allow_qemu:%s\n", reason);
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

/* Mint a recv handle for the broker-svc subject so this driver can
 * drain the broker port. Grants CAP_IPC_SEND on the legacy bitset
 * too so the audit-ring parity in ipc_recv_h is satisfied. */
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

/* Drain one envelope from the broker port, validate it's a
 * BROKER_OP_REQUEST from `expected_owner`, parse the payload, and
 * call cap_broker_request_share. On success writes the assigned
 * share id to `*out_sid`. */
static int driver_handle_request(ipc_port_t broker_port,
                                 cap_handle_t recv_h,
                                 cap_subject_id_t expected_owner,
                                 cap_share_id_t *out_sid) {
  ipc_msg_v0 rx = {0};
  ipc_result_t rr = ipc_recv_h(recv_h, broker_port, &rx);
  if (rr != IPC_OK) {
    fail("request_drain_not_ok");
    return 0;
  }
  if (rx.sender_subject != (uint32_t)expected_owner) {
    fail("request_drain_sender_mismatch");
    return 0;
  }
  if (rx.tag != (uint32_t)BROKER_OP_REQUEST) {
    fail("request_drain_wrong_tag");
    return 0;
  }
  if (rx.payload_len < 40u) {
    fail("request_drain_short_payload");
    return 0;
  }
  cap_subject_id_t recip = (cap_subject_id_t)ld_le32(&rx.payload[0]);
  capability_id_t  cap   = (capability_id_t)ld_le32(&rx.payload[4]);
  uint8_t          rlen  = rx.payload[8];
  if (rlen == 0u || rlen > 31u) {
    fail("request_drain_bad_resource_len");
    return 0;
  }
  /* Copy resource into a NUL-terminated buffer — cap_broker takes a
   * C string. */
  char resource[32] = {0};
  memcpy(resource, &rx.payload[9], rlen);

  cap_broker_result_t br =
      cap_broker_request_share(expected_owner, recip, cap, resource, out_sid);
  if (br != CAP_BROKER_OK) {
    fail("cap_broker_request_share_failed");
    return 0;
  }
  if (*out_sid == CAP_SHARE_ID_INVALID) {
    fail("cap_broker_request_share_invalid_id");
    return 0;
  }
  return 1;
}

/* Drain one envelope from the broker port, validate it's
 * BROKER_OP_APPROVE for `expected_sid` from `expected_owner`, and
 * call cap_broker_approve. */
static int driver_handle_approve(ipc_port_t broker_port,
                                 cap_handle_t recv_h,
                                 cap_subject_id_t expected_owner,
                                 cap_share_id_t expected_sid) {
  ipc_msg_v0 rx = {0};
  ipc_result_t rr = ipc_recv_h(recv_h, broker_port, &rx);
  if (rr != IPC_OK) {
    fail("approve_drain_not_ok");
    return 0;
  }
  if (rx.sender_subject != (uint32_t)expected_owner) {
    fail("approve_drain_sender_mismatch");
    return 0;
  }
  if (rx.tag != (uint32_t)BROKER_OP_APPROVE) {
    fail("approve_drain_wrong_tag");
    return 0;
  }
  if (rx.payload_len < 4u) {
    fail("approve_drain_short_payload");
    return 0;
  }
  cap_share_id_t sid = (cap_share_id_t)ld_le32(&rx.payload[0]);
  if (sid != expected_sid) {
    fail("approve_drain_share_id_mismatch");
    return 0;
  }
  cap_broker_result_t br = cap_broker_approve(expected_owner, sid);
  if (br != CAP_BROKER_OK) {
    fail("cap_broker_approve_failed");
    return 0;
  }
  return 1;
}

int main(void) {
  reset_world();

  /* broker_svc port allocation (slice 1, #302). */
  if (broker_svc_init() != BROKER_SVC_OK) {
    fail("broker_svc_init_failed");
    goto out;
  }
  ipc_port_t broker_port = broker_svc_port();
  if (broker_port == IPC_PORT_INVALID) {
    fail("broker_port_invalid");
    goto out;
  }

  /* broker-svc recv handle (acts as the broker recv loop). */
  cap_handle_t broker_recv = broker_svc_setup_recv();
  if (broker_recv == CAP_HANDLE_NULL) {
    fail("broker_svc_recv_setup_failed");
    goto out;
  }

  /* Slice 2: spawn the owner with the broker send handle stamped
   * into ipc_scratch[24..32). */
  const cap_subject_id_t owner    = (cap_subject_id_t)SUBJECT_M2_HELLOAPP;
  const cap_subject_id_t recip    = (cap_subject_id_t)6u; /* deliberately unrelated */
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
  if (sp.broker_handle == CAP_HANDLE_NULL) {
    fail("spawned_broker_handle_null");
    goto out;
  }
  if (cap_gate_check_handle(sp.broker_handle, CAP_IPC_SEND) != 1) {
    fail("spawned_broker_handle_gate_check");
    goto out;
  }

  /* owner_holds_cap_qemu — owner must hold the cap they are offering
   * (cap_broker_request_share's precondition). Mirrors sub-check 1 of
   * the host fixture. */
  if (cap_table_grant(owner, CAP_FS_READ) != CAP_OK) {
    fail("owner_grant_setup_failed");
    goto out;
  }
  if (cap_table_check(owner, CAP_FS_READ) != CAP_OK) {
    fail("owner_check_after_grant_missing");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_allow_qemu:owner_holds_cap_qemu\n");

  /* Owner sends BROKER_OP_REQUEST through the real ipc_send_h path. */
  helloapp_broker_owner_result_t ownr = {0};
  helloapp_entry_broker_owner(sp.aspace, broker_port,
                              recip, CAP_FS_READ,
                              "doc-alpha", 9u,
                              /*share_id_in=*/NULL,
                              &ownr);
  if (ownr.request_send_result != IPC_OK) {
    fail("owner_request_send_not_ok");
    goto out;
  }

  /* Driver drains the request envelope and fans into cap_broker. */
  cap_share_id_t sid = CAP_SHARE_ID_INVALID;
  if (!driver_handle_request(broker_port, broker_recv, owner, &sid)) {
    goto out;
  }
  /* Pre-approval, recipient must have nothing. */
  if (cap_broker_state_for_tests(sid) != CAP_SHARE_STATE_REQUESTED) {
    fail("state_not_requested_after_request");
    goto out;
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha")
        != CAP_ERR_MISSING) {
    fail("recipient_check_should_fail_before_approve");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_allow_qemu:request_returns_pending_share_id_qemu\n");

  /* Owner sends BROKER_OP_APPROVE for the assigned share id via the
   * discrete approve helper. (Calling helloapp_entry_broker_owner a
   * second time would also re-send a REQUEST envelope before the
   * APPROVE; the broker port can only stage one envelope at a time,
   * so we use the dedicated helper instead.) */
  if (helloapp_entry_broker_owner_approve(sp.aspace, broker_port, sid)
        != IPC_OK) {
    fail("owner_approve_send_not_ok");
    goto out;
  }
  if (!driver_handle_approve(broker_port, broker_recv, owner, sid)) {
    goto out;
  }

  if (cap_broker_state_for_tests(sid) != CAP_SHARE_STATE_APPROVED) {
    fail("state_not_approved_after_approve");
    goto out;
  }
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-alpha") != CAP_OK) {
    fail("recipient_check_after_approve_failed");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_allow_qemu:approve_grants_recipient_qemu\n");

  /* scope_is_resource_bound_qemu — different resource name must miss. */
  if (cap_broker_recipient_check(recip, CAP_FS_READ, "doc-beta")
        != CAP_ERR_MISSING) {
    fail("other_resource_should_not_be_granted");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_allow_qemu:scope_is_resource_bound_qemu\n");

  /* scope_is_capability_bound_qemu — different cap must miss. */
  if (cap_broker_recipient_check(recip, CAP_FS_WRITE, "doc-alpha")
        != CAP_ERR_MISSING) {
    fail("other_capability_should_not_be_granted");
    goto out;
  }
  printf("TEST:PASS:m4_broker_share_allow_qemu:scope_is_capability_bound_qemu\n");

  if (launcher_broker_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("broker_spawn_destroy_failed");
    goto out;
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:m4_broker_share_allow_qemu\n");
  return 0;
}
