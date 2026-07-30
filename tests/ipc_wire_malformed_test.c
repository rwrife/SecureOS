/**
 * @file ipc_wire_malformed_test.c
 * @brief Host fixture for malformed IPC envelope rejection semantics.
 *
 * Purpose:
 *   Pins the malformed-envelope branches documented by
 *   docs/abi/ipc-wire.md §5 (IPC_ERR_INVALID_MSG) for the current v0
 *   envelope surface:
 *     - NULL outbound envelope pointer
 *     - abi_version mismatch
 *     - reserved flags (MBZ) violation
 *     - payload_len overflow
 *     - delivered envelope with sender_subject == 0
 *
 *   For each malformed case, this harness additionally proves the
 *   rendezvous slot stays usable by immediately round-tripping a valid
 *   send/recv after the rejection.
 *
 * Notes:
 *   The v0 IPC surface consumes typed `ipc_msg_v0` envelopes, so
 *   byte-stream framing failures like "truncated header" / "unknown
 *   opcode" are not representable yet. This test therefore locks down
 *   the malformed cases that are currently normative in the ABI spec.
 *
 * Interactions:
 *   - kernel/ipc/ipc_ops.c   (ipc_send/ipc_recv validation paths)
 *   - kernel/ipc/ipc_port.c  (port slot staging/consume helper)
 *   - kernel/cap/ subsystem  (CAP_IPC_SEND/CAP_IPC_RECV gating)
 *
 * Launched by:
 *   build/scripts/test_ipc_wire_malformed.sh
 *   (dispatched via build/scripts/test.sh and validate_bundle.sh).
 *
 * Issue: #586.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/ipc/ipc_msg.h"
#include "../kernel/ipc/ipc_ops.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/proc/process.h"

typedef struct {
  cap_subject_id_t sender;
  cap_subject_id_t receiver;
  ipc_port_t port;
} ipc_fixture_t;

static int g_failures = 0;

#define CHECK(cond, label)                                                        \
  do {                                                                             \
    if (!(cond)) {                                                                 \
      printf("TEST:FAIL:ipc_wire_malformed:%s\n", (label));                      \
      g_failures++;                                                                \
    }                                                                              \
  } while (0)

static void reset_world(void) {
  cap_reset_for_tests();
  cap_table_reset();
  ipc_port_table_reset();
  process_table_reset();
}

static void make_valid_msg(ipc_msg_v0 *m, const char *payload) {
  memset(m, 0, sizeof(*m));
  m->abi_version = (uint16_t)OS_ABI_VERSION;
  m->flags = 0u;
  m->sender_subject = 0xA5A5A5A5u; /* ipc_send() overwrites this on success. */
  m->tag = 0x10203040u;

  size_t len = strlen(payload);
  if (len > IPC_MSG_PAYLOAD_MAX) {
    len = IPC_MSG_PAYLOAD_MAX;
  }
  memcpy(m->payload, payload, len);
  m->payload_len = (uint32_t)len;
}

static int setup_fixture(ipc_fixture_t *f,
                         cap_subject_id_t sender,
                         cap_subject_id_t receiver) {
  reset_world();

  f->sender = sender;
  f->receiver = receiver;
  f->port = IPC_PORT_INVALID;

  if (cap_table_grant(sender, CAP_IPC_SEND) != CAP_OK) {
    return 0;
  }
  if (cap_table_grant(receiver, CAP_IPC_RECV) != CAP_OK) {
    return 0;
  }
  if (ipc_port_create(receiver, CAP_IPC_SEND, CAP_IPC_RECV, &f->port) != IPC_OK) {
    return 0;
  }
  return f->port != IPC_PORT_INVALID;
}

static void assert_valid_round_trip(const ipc_fixture_t *f, const char *label_prefix) {
  ipc_msg_v0 out;
  make_valid_msg(&out, "ok");
  ipc_result_t sr = ipc_send(f->sender, f->port, &out);
  if (sr != IPC_OK) {
    printf("TEST:FAIL:ipc_wire_malformed:%s:post_reject_send_rc=%d\n",
           label_prefix, (int)sr);
    g_failures++;
    return;
  }

  ipc_msg_v0 in;
  memset(&in, 0, sizeof(in));
  ipc_result_t rr = ipc_recv(f->receiver, f->port, &in);
  if (rr != IPC_OK) {
    printf("TEST:FAIL:ipc_wire_malformed:%s:post_reject_recv_rc=%d\n",
           label_prefix, (int)rr);
    g_failures++;
    return;
  }

  if (in.payload_len != 2u || memcmp(in.payload, "ok", 2u) != 0) {
    printf("TEST:FAIL:ipc_wire_malformed:%s:post_reject_payload_drift\n",
           label_prefix);
    g_failures++;
  }
}

static void test_null_outbound_pointer(void) {
  ipc_fixture_t f;
  CHECK(setup_fixture(&f, 1u, 2u), "null_pointer:setup");

  ipc_result_t rc = ipc_send(f.sender, f.port, NULL);
  CHECK(rc == IPC_ERR_INVALID_MSG, "null_pointer:rc_invalid_msg");
  CHECK(!ipc_port_has_pending_for_tests(f.port), "null_pointer:no_pending_slot");
  assert_valid_round_trip(&f, "null_pointer");

  printf("TEST:PASS:ipc_wire_malformed:null_message_pointer_returns_invalid_msg\n");
}

static void test_abi_version_mismatch(void) {
  ipc_fixture_t f;
  CHECK(setup_fixture(&f, 3u, 4u), "abi_mismatch:setup");

  ipc_msg_v0 bad;
  make_valid_msg(&bad, "abi");
  bad.abi_version = (uint16_t)(OS_ABI_VERSION + 1u);

  ipc_result_t rc = ipc_send(f.sender, f.port, &bad);
  CHECK(rc == IPC_ERR_INVALID_MSG, "abi_mismatch:rc_invalid_msg");
  CHECK(!ipc_port_has_pending_for_tests(f.port), "abi_mismatch:no_pending_slot");
  assert_valid_round_trip(&f, "abi_mismatch");

  printf("TEST:PASS:ipc_wire_malformed:abi_version_mismatch_returns_invalid_msg\n");
}

static void test_reserved_flags_rejected(void) {
  ipc_fixture_t f;
  CHECK(setup_fixture(&f, 5u, 6u), "reserved_flags:setup");

  ipc_msg_v0 bad;
  make_valid_msg(&bad, "flags");
  bad.flags = 0x1u;

  ipc_result_t rc = ipc_send(f.sender, f.port, &bad);
  CHECK(rc == IPC_ERR_INVALID_MSG, "reserved_flags:rc_invalid_msg");
  CHECK(!ipc_port_has_pending_for_tests(f.port), "reserved_flags:no_pending_slot");
  assert_valid_round_trip(&f, "reserved_flags");

  printf("TEST:PASS:ipc_wire_malformed:reserved_flags_returns_invalid_msg\n");
}

static void test_payload_len_overflow(void) {
  ipc_fixture_t f;
  CHECK(setup_fixture(&f, 1u, 2u), "payload_overflow:setup");

  ipc_msg_v0 bad;
  make_valid_msg(&bad, "len");
  bad.payload_len = IPC_MSG_PAYLOAD_MAX + 1u;

  ipc_result_t rc = ipc_send(f.sender, f.port, &bad);
  CHECK(rc == IPC_ERR_INVALID_MSG, "payload_overflow:rc_invalid_msg");
  CHECK(!ipc_port_has_pending_for_tests(f.port), "payload_overflow:no_pending_slot");
  assert_valid_round_trip(&f, "payload_overflow");

  printf("TEST:PASS:ipc_wire_malformed:oversized_payload_returns_invalid_msg\n");
}

static void test_zero_sender_subject_on_delivery(void) {
  ipc_fixture_t f;
  CHECK(setup_fixture(&f, 3u, 4u), "sender_zero_delivery:setup");

  ipc_msg_v0 staged;
  make_valid_msg(&staged, "raw");
  staged.sender_subject = 0u; /* §2.4 malformed on delivery. */

  ipc_result_t sr = ipc_port_stage(f.port, &staged);
  CHECK(sr == IPC_OK, "sender_zero_delivery:stage_ok");

  ipc_msg_v0 out;
  memset(&out, 0xA5, sizeof(out));
  ipc_result_t rc = ipc_recv(f.receiver, f.port, &out);
  CHECK(rc == IPC_ERR_INVALID_MSG, "sender_zero_delivery:recv_invalid_msg");
  CHECK(!ipc_port_has_pending_for_tests(f.port), "sender_zero_delivery:slot_drained");

  /* Ensure the port still works after rejecting the malformed delivery. */
  assert_valid_round_trip(&f, "sender_zero_delivery");

  printf("TEST:PASS:ipc_wire_malformed:sender_subject_zero_on_delivery_returns_invalid_msg\n");
}

int main(void) {
  printf("TEST:START:ipc_wire_malformed\n");
  test_null_outbound_pointer();
  test_abi_version_mismatch();
  test_reserved_flags_rejected();
  test_payload_len_overflow();
  test_zero_sender_subject_on_delivery();

  if (g_failures != 0) {
    printf("TEST:FAIL:ipc_wire_malformed:summary_failures=%d\n", g_failures);
    return 1;
  }

  printf("TEST:PASS:ipc_wire_malformed\n");
  return 0;
}
