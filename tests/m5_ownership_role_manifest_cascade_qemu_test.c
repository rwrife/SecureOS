/* tests/m5_ownership_role_manifest_cascade_qemu_test.c
 *
 * Substrate peer for issue #585 ownership-role runtime wiring.
 *
 * Validates end-to-end cascade semantics rooted at the launcher owner
 * handle for broker-spawned apps:
 *   - ownership_role=owner: launcher-root delete cascades through the
 *     app broker handle and stales delegated handles.
 *   - ownership_role=delegate: delegated handles still stale under the
 *     same launcher-root cascade.
 *
 * Called by:
 *   build/scripts/test_m5_ownership_role_manifest_cascade_qemu.sh
 */

#include <stdint.h>
#include <stdio.h>
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
  printf("TEST:FAIL:m5_ownership_role_manifest_cascade_qemu:%s\n", reason);
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

static cap_handle_t seed_launcher_root_handle(void) {
  if (cap_table_grant((cap_subject_id_t)SUBJECT_M2_LAUNCHER,
                      CAP_IPC_SEND) != CAP_OK) {
    return CAP_HANDLE_NULL;
  }
  return cap_handle_grant((cap_subject_id_t)SUBJECT_M2_LAUNCHER,
                          CAP_IPC_SEND);
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

static int driver_handle_request(ipc_port_t broker_port,
                                 cap_handle_t recv_h,
                                 cap_subject_id_t expected_owner,
                                 cap_subject_id_t *out_recip,
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
    fail("request_bad_resource_len");
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

static int driver_handle_approve_h(ipc_port_t broker_port,
                                   cap_handle_t recv_h,
                                   cap_subject_id_t expected_owner,
                                   cap_subject_id_t recipient,
                                   cap_share_id_t expected_sid,
                                   cap_handle_t owner_broker_h,
                                   cap_handle_t *out_recipient_h) {
  ipc_msg_v0 rx = {0};
  if (ipc_recv_h(recv_h, broker_port, &rx) != IPC_OK) {
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
    fail("approve_share_id_mismatch");
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

static int run_case(launcher_ownership_role_t role,
                    int expect_owner_handle_revoked,
                    const char *pass_marker) {
  const cap_subject_id_t owner = (cap_subject_id_t)SUBJECT_M2_HELLOAPP;
  const cap_subject_id_t recipient = (cap_subject_id_t)7u;

  reset_world();

  if (broker_svc_init() != BROKER_SVC_OK) {
    fail("broker_svc_init_failed");
    return 0;
  }
  if (fs_svc_init() != FS_SVC_OK) {
    fail("fs_svc_init_failed");
    return 0;
  }

  ipc_port_t broker_port = broker_svc_port();
  if (broker_port == IPC_PORT_INVALID) {
    fail("broker_port_invalid");
    return 0;
  }

  cap_handle_t broker_recv = broker_svc_setup_recv();
  if (broker_recv == CAP_HANDLE_NULL) {
    fail("broker_recv_setup_failed");
    return 0;
  }

  cap_handle_t launcher_root = seed_launcher_root_handle();
  if (launcher_root == CAP_HANDLE_NULL) {
    fail("launcher_root_seed_failed");
    return 0;
  }

  launcher_manifest_t m = {
      .subject_id = owner,
      .auto_grant_caps = NULL,
      .auto_grant_count = 0u,
      .arena_bytes = 0u,
      .ownership_role = role,
  };

  launcher_broker_spawn_t sp;
  memset(&sp, 0, sizeof sp);
  if (launcher_broker_spawn_app_with_broker_cap(&m, &sp) != LAUNCHER_OK) {
    fail("launcher_broker_spawn_failed");
    return 0;
  }

  if (cap_table_grant(owner, CAP_FS_READ) != CAP_OK) {
    fail("owner_fs_read_grant_failed");
    return 0;
  }

  helloapp_broker_owner_result_t ownr = {0};
  helloapp_entry_broker_owner(sp.aspace, broker_port,
                              recipient, CAP_FS_READ,
                              "doc-alpha", 9u,
                              NULL,
                              &ownr);
  if (ownr.request_send_result != IPC_OK) {
    fail("owner_request_send_not_ok");
    return 0;
  }

  cap_subject_id_t drained_recip = 0u;
  cap_share_id_t sid = CAP_SHARE_ID_INVALID;
  if (!driver_handle_request(broker_port, broker_recv, owner,
                             &drained_recip, &sid)) {
    return 0;
  }
  if (drained_recip != recipient) {
    fail("request_recipient_mismatch");
    return 0;
  }

  if (helloapp_entry_broker_owner_approve(sp.aspace, broker_port, sid) != IPC_OK) {
    fail("owner_approve_send_not_ok");
    return 0;
  }

  cap_handle_t recipient_h = CAP_HANDLE_NULL;
  if (!driver_handle_approve_h(broker_port, broker_recv, owner,
                               recipient, sid, sp.broker_handle,
                               &recipient_h)) {
    return 0;
  }

  if (cap_gate_check_handle(recipient_h, CAP_FS_READ) != 1) {
    fail("recipient_handle_pre_gate_failed");
    return 0;
  }

  uint32_t n_children = 0u;
  broker_svc_result_t dr =
      broker_svc_delete_owner((cap_subject_id_t)SUBJECT_M2_LAUNCHER,
                              (cap_subject_id_t)SUBJECT_M2_LAUNCHER,
                              launcher_root,
                              PID_INVALID,
                              &n_children);
  if (dr != BROKER_SVC_OK) {
    fail("launcher_delete_owner_failed");
    return 0;
  }

  cap_result_t owner_post =
      cap_gate_check_handle_result(sp.broker_handle, CAP_IPC_SEND);
  cap_result_t recip_post =
      cap_gate_check_handle_result(recipient_h, CAP_FS_READ);

  if (expect_owner_handle_revoked) {
    if (owner_post != CAP_ERR_MISSING) {
      fail("owner_handle_expected_revoked");
      return 0;
    }
  }

  if (recip_post != CAP_ERR_MISSING) {
    fail("recipient_handle_expected_revoked");
    return 0;
  }

  if (launcher_broker_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("launcher_broker_spawn_destroy_failed");
    return 0;
  }

  printf("TEST:PASS:%s\n", pass_marker);
  return 1;
}

int main(void) {
  if (!run_case(LAUNCHER_OWNERSHIP_ROLE_OWNER,
                /*expect_owner_handle_revoked=*/1,
                "m5_ownership_role_owner_cascade_qemu")) {
    return 1;
  }

  if (!run_case(LAUNCHER_OWNERSHIP_ROLE_DELEGATE,
                /*expect_owner_handle_revoked=*/1,
                "m5_ownership_role_delegate_caps_invalid_qemu")) {
    return 1;
  }

  if (g_fail) {
    return 1;
  }

  puts("TEST:PASS:m5_ownership_role_manifest_cascade_qemu");
  return 0;
}
