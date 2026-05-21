/**
 * @file ipc_sync_v0_test.c
 * @brief Acceptance test for the M1 synchronous IPC primitive v0 (#210).
 *
 * Covers the done-when bullets from issue #210 / plan
 * `plans/2026-05-19-m1-sync-ipc-primitive.md`:
 *
 *   1. Allow path: two in-kernel test modules holding CAP_IPC_SEND /
 *      CAP_IPC_RECV exchange one envelope. Asserts the receiver sees
 *      the kernel-stamped sender_subject and the original payload.
 *      Emits TEST:PASS:ipc_sync_v0_allow_send_recv.
 *
 *   2. Deny path: caller missing CAP_IPC_SEND gets IPC_ERR_CAP_DENIED
 *      and the canonical `CAP:DENY:<subject>:<cap>:-` marker per
 *      docs/abi/capability-deny-contract.md is emitted before return.
 *      Emits TEST:PASS:ipc_sync_v0_deny_marker.
 *
 *   3. ipc_call round-trip over a caller-owned reply port. Emits
 *      TEST:PASS:ipc_sync_v0_call_round_trip.
 *
 *   4. ABI surface: envelope size and field offsets match the spec
 *      (static_asserts in ipc_msg.h are the primary guard; this test
 *      also confirms at run time and asserts IPC_MSG_PAYLOAD_MAX == 64
 *      and OS_ABI_VERSION acceptance). Emits
 *      TEST:PASS:ipc_sync_v0_abi_envelope.
 *
 *   5. Aggregate marker TEST:PASS:ipc_sync_v0 is the rollup the harness
 *      asserts, matching the per-target naming convention.
 *
 * Launched by:
 *   build/scripts/test_ipc_sync_v0.sh (dispatched via test.sh and
 *   validate_bundle.sh).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/capability.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/ipc/ipc_msg.h"
#include "../kernel/ipc/ipc_ops.h"
#include "../kernel/ipc/ipc_port.h"

static void die(const char *reason) {
  printf("TEST:FAIL:ipc_sync_v0:%s\n", reason);
  exit(1);
}

static void reset_world(void) {
  cap_reset_for_tests();
  cap_table_reset();
  ipc_port_table_reset();
  cap_audit_reset_for_tests();
}

static void make_msg(ipc_msg_v0 *m, uint32_t tag, const char *payload) {
  memset(m, 0, sizeof(*m));
  m->abi_version = (uint16_t)OS_ABI_VERSION;
  m->flags = 0u;
  m->sender_subject = 0u; /* kernel-stamped on send (§2.4) */
  m->tag = tag;
  size_t len = strlen(payload);
  if (len > IPC_MSG_PAYLOAD_MAX) {
    len = IPC_MSG_PAYLOAD_MAX;
  }
  memcpy(m->payload, payload, len);
  m->payload_len = (uint32_t)len;
}

static void test_abi_envelope(void) {
  if (sizeof(ipc_msg_v0) != IPC_MSG_V0_SIZE) {
    die("envelope_size_mismatch");
  }
  if (IPC_MSG_PAYLOAD_MAX != 64u) {
    die("payload_max_must_be_64");
  }
  /* Probe a value that's not OS_ABI_VERSION to confirm receiver
   * rejection on abi_version mismatch (spec §2.1). */
  ipc_port_t port = IPC_PORT_INVALID;
  if (ipc_port_create(7u, CAP_IPC_SEND, CAP_IPC_RECV, &port) != IPC_OK) {
    die("port_create_abi");
  }
  cap_table_reset();
  if (cap_table_grant(7u, CAP_IPC_SEND) != CAP_OK) {
    die("grant_send_abi");
  }
  ipc_msg_v0 bad;
  make_msg(&bad, 0u, "x");
  bad.abi_version = (uint16_t)(OS_ABI_VERSION + 0x1u);
  if (ipc_send(7u, port, &bad) != IPC_ERR_INVALID_MSG) {
    die("abi_version_not_rejected");
  }
  bad.abi_version = (uint16_t)OS_ABI_VERSION;
  bad.flags = 0x1u;
  if (ipc_send(7u, port, &bad) != IPC_ERR_INVALID_MSG) {
    die("flags_mbz_not_enforced");
  }
  bad.flags = 0u;
  bad.payload_len = IPC_MSG_PAYLOAD_MAX + 1u;
  if (ipc_send(7u, port, &bad) != IPC_ERR_INVALID_MSG) {
    die("oversize_payload_not_rejected");
  }
  printf("TEST:PASS:ipc_sync_v0_abi_envelope\n");
}

static void test_allow_send_recv(void) {
  const cap_subject_id_t sender = 2u;
  const cap_subject_id_t receiver = 3u;

  reset_world();
  if (cap_table_grant(sender, CAP_IPC_SEND) != CAP_OK) {
    die("grant_send");
  }
  if (cap_table_grant(receiver, CAP_IPC_RECV) != CAP_OK) {
    die("grant_recv");
  }

  ipc_port_t port = IPC_PORT_INVALID;
  if (ipc_port_create(receiver, CAP_IPC_SEND, CAP_IPC_RECV, &port) != IPC_OK || port == IPC_PORT_INVALID) {
    die("port_create");
  }

  ipc_msg_v0 out;
  make_msg(&out, 0xCAFEu, "hello-ipc-v0");
  /* Caller fills sender_subject with a deliberately wrong value; the
   * IPC layer must overwrite it with the authenticated subject. */
  out.sender_subject = 0xDEADBEEFu;
  if (ipc_send(sender, port, &out) != IPC_OK) {
    die("send_allow_failed");
  }
  if (!ipc_port_has_pending_for_tests(port)) {
    die("envelope_not_staged");
  }

  ipc_msg_v0 in;
  memset(&in, 0xAA, sizeof(in));
  if (ipc_recv(receiver, port, &in) != IPC_OK) {
    die("recv_allow_failed");
  }
  if (in.abi_version != (uint16_t)OS_ABI_VERSION) {
    die("abi_version_lost");
  }
  if (in.sender_subject != sender) {
    die("sender_not_kernel_stamped");
  }
  if (in.tag != 0xCAFEu) {
    die("tag_corrupted");
  }
  if (in.payload_len != strlen("hello-ipc-v0")) {
    die("payload_len_corrupted");
  }
  if (memcmp(in.payload, "hello-ipc-v0", in.payload_len) != 0) {
    die("payload_corrupted");
  }
  printf("TEST:PASS:ipc_sync_v0_allow_send_recv\n");
}

static void test_deny_marker(void) {
  const cap_subject_id_t sender = 4u;
  const cap_subject_id_t receiver = 5u;

  reset_world();
  /* Deliberately do NOT grant CAP_IPC_SEND to `sender`. */
  if (cap_table_grant(receiver, CAP_IPC_RECV) != CAP_OK) {
    die("grant_recv_deny_setup");
  }

  ipc_port_t port = IPC_PORT_INVALID;
  if (ipc_port_create(receiver, CAP_IPC_SEND, CAP_IPC_RECV, &port) != IPC_OK) {
    die("port_create_deny");
  }

  ipc_msg_v0 m;
  make_msg(&m, 0u, "denied");

  /* ipc_send must emit a canonical CAP:DENY marker on stdout *before*
   * returning IPC_ERR_CAP_DENIED. The test script greps the captured
   * log for the exact marker (CAP:DENY:4:ipc_send:-); here we only
   * assert the return code and the audit-ring side effect. */
  ipc_result_t r = ipc_send(sender, port, &m);
  if (r != IPC_ERR_CAP_DENIED) {
    die("deny_wrong_result");
  }

  /* Audit ring must have recorded the deny event. */
  size_t count = cap_audit_count_for_tests();
  cap_audit_event_t ev;
  int found = 0;
  for (size_t i = 0; i < count; ++i) {
    if (cap_audit_get_for_tests(i, &ev) != CAP_OK) {
      continue;
    }
    if (ev.operation == CAP_AUDIT_OP_CHECK
        && ev.actor_subject_id == sender
        && ev.capability_id == CAP_IPC_SEND
        && ev.result == CAP_ERR_MISSING) {
      found = 1;
      break;
    }
  }
  if (!found) {
    die("deny_audit_missing");
  }
  printf("TEST:PASS:ipc_sync_v0_deny_marker\n");
}

static void test_call_round_trip(void) {
  const cap_subject_id_t client = 2u;
  const cap_subject_id_t server = 3u;

  reset_world();
  if (cap_table_grant(client, CAP_IPC_SEND) != CAP_OK) {
    die("grant_client_send");
  }
  if (cap_table_grant(client, CAP_IPC_RECV) != CAP_OK) {
    die("grant_client_recv");
  }
  if (cap_table_grant(server, CAP_IPC_SEND) != CAP_OK) {
    die("grant_server_send");
  }
  if (cap_table_grant(server, CAP_IPC_RECV) != CAP_OK) {
    die("grant_server_recv");
  }

  ipc_port_t service_port = IPC_PORT_INVALID;
  ipc_port_t reply_port = IPC_PORT_INVALID;
  if (ipc_port_create(server, CAP_IPC_SEND, CAP_IPC_RECV, &service_port) != IPC_OK) {
    die("service_port_create");
  }
  if (ipc_port_create(client, CAP_IPC_SEND, CAP_IPC_RECV, &reply_port) != IPC_OK) {
    die("reply_port_create");
  }

  /* Client builds the request. The reply-port handle is also written
   * into `tag` for forward compatibility with the §2.3 reply-port
   * encoding; v0 carries it as an explicit argument too. */
  ipc_msg_v0 req;
  make_msg(&req, reply_port, "PING");

  /* We can't return from ipc_call cleanly here because ipc_send
   * blocks-by-policy on a full slot, but the v0 in-kernel scaffold
   * stages-and-returns. So we drive the round-trip explicitly: client
   * sends request, server consumes + sends reply, client recvs. This
   * is what ipc_call sequences internally — we additionally check
   * each leg by hand. */
  if (ipc_send(client, service_port, &req) != IPC_OK) {
    die("call_send_req");
  }
  ipc_msg_v0 server_in;
  if (ipc_recv(server, service_port, &server_in) != IPC_OK) {
    die("call_server_recv");
  }
  if (server_in.sender_subject != client) {
    die("call_sender_mismatch");
  }
  if (server_in.tag != reply_port) {
    die("call_tag_lost_reply_handle");
  }

  ipc_msg_v0 reply;
  make_msg(&reply, server_in.tag, "PONG");
  if (ipc_send(server, reply_port, &reply) != IPC_OK) {
    die("call_send_reply");
  }
  ipc_msg_v0 client_in;
  if (ipc_recv(client, reply_port, &client_in) != IPC_OK) {
    die("call_client_recv");
  }
  if (client_in.sender_subject != server) {
    die("call_reply_sender");
  }
  if (client_in.payload_len != 4u || memcmp(client_in.payload, "PONG", 4) != 0) {
    die("call_reply_payload");
  }

  /* Now exercise the ipc_call wrapper end-to-end with a fresh pair.
   * Client sends, server picks it up out-of-band, replies, and client
   * recvs through the wrapper. We split into two send/recv windows so
   * the single-waiter slot model is honored. */
  reset_world();
  (void)cap_table_grant(client, CAP_IPC_SEND);
  (void)cap_table_grant(client, CAP_IPC_RECV);
  (void)cap_table_grant(server, CAP_IPC_SEND);
  (void)cap_table_grant(server, CAP_IPC_RECV);
  if (ipc_port_create(server, CAP_IPC_SEND, CAP_IPC_RECV, &service_port) != IPC_OK) {
    die("wrap_service_port");
  }
  if (ipc_port_create(client, CAP_IPC_SEND, CAP_IPC_RECV, &reply_port) != IPC_OK) {
    die("wrap_reply_port");
  }
  /* Pre-stage server's reply *before* the client calls so the round
   * trip completes without a scheduler. The client first send-stages
   * a request that the server will never explicitly drain in this
   * harness (it's a v0-only simplification); after the send the
   * client's recv will pick up our pre-staged reply. We then drain
   * the request from the service port to clean up. */
  ipc_msg_v0 pre_reply;
  make_msg(&pre_reply, reply_port, "PONG2");
  if (ipc_send(server, reply_port, &pre_reply) != IPC_OK) {
    die("prestage_reply");
  }

  ipc_msg_v0 wrap_req;
  make_msg(&wrap_req, reply_port, "PING2");
  ipc_msg_v0 wrap_reply;
  if (ipc_call(client, service_port, &wrap_req, reply_port, &wrap_reply) != IPC_OK) {
    die("ipc_call_failed");
  }
  if (wrap_reply.payload_len != 5u || memcmp(wrap_reply.payload, "PONG2", 5) != 0) {
    die("ipc_call_payload");
  }
  if (wrap_reply.sender_subject != server) {
    die("ipc_call_sender");
  }

  /* Drain the staged request from the service port to leave a clean
   * table for any later tests. */
  ipc_msg_v0 drain;
  (void)ipc_recv(server, service_port, &drain);

  printf("TEST:PASS:ipc_sync_v0_call_round_trip\n");
}

int main(void) {
  printf("TEST:START:ipc_sync_v0\n");
  ipc_port_table_init();
  test_abi_envelope();
  test_allow_send_recv();
  test_deny_marker();
  test_call_round_trip();
  printf("TEST:PASS:ipc_sync_v0\n");
  return 0;
}
