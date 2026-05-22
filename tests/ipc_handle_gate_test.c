/**
 * @file ipc_handle_gate_test.c
 * @brief Acceptance test for handle-gated IPC ops (M1-CAPTBL-006, issue #246).
 *
 * Covers the done-when bullets from issue #246:
 *
 *   1. Allow path: cap_handle_grant(S, CAP_IPC_SEND) -> ipc_send_h with
 *      the resulting handle stages an envelope, and the kernel-stamped
 *      sender_subject equals the handle's owner (not a caller-supplied
 *      argument). Receiver uses ipc_recv_h with a CAP_IPC_RECV handle.
 *      Emits TEST:PASS:ipc_handle_gate_allow.
 *
 *   2. Wrong-cap-handle path: handle granted for CAP_CONSOLE_WRITE
 *      routed to ipc_send_h -> IPC_ERR_CAP_DENIED + canonical marker.
 *      Emits TEST:PASS:ipc_handle_gate_deny_wrong_cap.
 *
 *   3. Stale-handle path: grant -> cap_handle_revoke -> re-call with
 *      the now-stale handle -> IPC_ERR_CAP_DENIED + marker.
 *      Emits TEST:PASS:ipc_handle_gate_deny_stale.
 *
 *   4. Wrong-owner-on-recv path: ipc_recv_h called with a handle whose
 *      owner != port owner -> IPC_ERR_CAP_DENIED + marker.
 *      Emits TEST:PASS:ipc_handle_gate_deny_wrong_owner.
 *
 *   5. Aggregate marker TEST:PASS:ipc_handle_gate is the harness
 *      rollup.
 *
 * Launched by:
 *   build/scripts/test_ipc_handle_gate.sh (dispatched via test.sh and
 *   validate_bundle.sh).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/capability.h"
#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/ipc/ipc_msg.h"
#include "../kernel/ipc/ipc_ops.h"
#include "../kernel/ipc/ipc_port.h"

static void die(const char *reason) {
  printf("TEST:FAIL:ipc_handle_gate:%s\n", reason);
  exit(1);
}

static void reset_world(void) {
  cap_reset_for_tests();
  cap_table_reset();
  cap_handle_table_reset();
  ipc_port_table_reset();
  cap_audit_reset_for_tests();
}

static void make_msg(ipc_msg_v0 *m, uint32_t tag, const char *payload) {
  memset(m, 0, sizeof(*m));
  m->abi_version = (uint16_t)OS_ABI_VERSION;
  m->flags = 0u;
  m->sender_subject = 0u;
  m->tag = tag;
  size_t len = strlen(payload);
  if (len > IPC_MSG_PAYLOAD_MAX) {
    len = IPC_MSG_PAYLOAD_MAX;
  }
  memcpy(m->payload, payload, len);
  m->payload_len = (uint32_t)len;
}

static void test_allow(void) {
  const cap_subject_id_t sender = 1u;
  const cap_subject_id_t receiver = 2u;

  reset_world();
  /* Grant via cap_table for the legacy bitset (which the allow-path
   * audit parity hit also touches), AND via cap_handle for the
   * handle-keyed decision. Both layers are required because the
   * gated path makes the handle authoritative for the decision and
   * delegates the audit ring write to cap_check for byte-identical
   * audit output. */
  if (cap_table_grant(sender, CAP_IPC_SEND) != CAP_OK) {
    die("grant_send_legacy");
  }
  if (cap_table_grant(receiver, CAP_IPC_RECV) != CAP_OK) {
    die("grant_recv_legacy");
  }
  cap_handle_t send_h = cap_handle_grant(sender, CAP_IPC_SEND);
  if (send_h == CAP_HANDLE_NULL) {
    die("handle_grant_send");
  }
  cap_handle_t recv_h = cap_handle_grant(receiver, CAP_IPC_RECV);
  if (recv_h == CAP_HANDLE_NULL) {
    die("handle_grant_recv");
  }

  ipc_port_t port = IPC_PORT_INVALID;
  if (ipc_port_create(receiver, CAP_IPC_SEND, CAP_IPC_RECV, &port) != IPC_OK
      || port == IPC_PORT_INVALID) {
    die("port_create");
  }

  ipc_msg_v0 out;
  make_msg(&out, 0xC0DEu, "hello-handle-gate");
  /* Deliberately set sender_subject to an attacker-supplied value;
   * the handle-gated path must overwrite with the handle's owner. */
  out.sender_subject = 0xFEEDFACEu;
  if (ipc_send_h(send_h, port, &out) != IPC_OK) {
    die("send_h_allow_failed");
  }

  ipc_msg_v0 in;
  memset(&in, 0xAA, sizeof(in));
  if (ipc_recv_h(recv_h, port, &in) != IPC_OK) {
    die("recv_h_allow_failed");
  }
  if (in.sender_subject != sender) {
    die("sender_not_handle_owner");
  }
  if (in.tag != 0xC0DEu) {
    die("tag_corrupted");
  }
  if (in.payload_len != strlen("hello-handle-gate")) {
    die("payload_len_corrupted");
  }
  if (memcmp(in.payload, "hello-handle-gate", in.payload_len) != 0) {
    die("payload_corrupted");
  }
  printf("TEST:PASS:ipc_handle_gate_allow\n");
}

static void test_deny_wrong_cap(void) {
  const cap_subject_id_t sender = 3u;
  const cap_subject_id_t receiver = 4u;

  reset_world();
  /* Grant a non-IPC cap so the handle resolves but the cap_id check
   * inside cap_gate_check_handle_result rejects it. */
  cap_handle_t bad_h = cap_handle_grant(sender, CAP_CONSOLE_WRITE);
  if (bad_h == CAP_HANDLE_NULL) {
    die("handle_grant_console");
  }

  ipc_port_t port = IPC_PORT_INVALID;
  if (ipc_port_create(receiver, CAP_IPC_SEND, CAP_IPC_RECV, &port) != IPC_OK) {
    die("port_create_deny_cap");
  }

  ipc_msg_v0 m;
  make_msg(&m, 0u, "denied-cap");

  ipc_result_t r = ipc_send_h(bad_h, port, &m);
  if (r != IPC_ERR_CAP_DENIED) {
    die("wrong_cap_not_denied");
  }
  if (ipc_port_has_pending_for_tests(port)) {
    die("envelope_leaked_on_deny");
  }
  printf("TEST:PASS:ipc_handle_gate_deny_wrong_cap\n");
}

static void test_deny_stale(void) {
  const cap_subject_id_t sender = 5u;
  const cap_subject_id_t receiver = 6u;

  reset_world();
  cap_handle_t send_h = cap_handle_grant(sender, CAP_IPC_SEND);
  if (send_h == CAP_HANDLE_NULL) {
    die("handle_grant_stale_setup");
  }
  /* Revoke makes the same numeric handle stale (generation bumped). */
  if (cap_handle_revoke(send_h) != CAP_OK) {
    die("handle_revoke_setup");
  }

  ipc_port_t port = IPC_PORT_INVALID;
  if (ipc_port_create(receiver, CAP_IPC_SEND, CAP_IPC_RECV, &port) != IPC_OK) {
    die("port_create_stale");
  }

  ipc_msg_v0 m;
  make_msg(&m, 0u, "denied-stale");

  ipc_result_t r = ipc_send_h(send_h, port, &m);
  if (r != IPC_ERR_CAP_DENIED) {
    die("stale_handle_not_denied");
  }
  if (ipc_port_has_pending_for_tests(port)) {
    die("envelope_leaked_on_stale_deny");
  }
  printf("TEST:PASS:ipc_handle_gate_deny_stale\n");
}

static void test_deny_wrong_owner_on_recv(void) {
  const cap_subject_id_t port_owner = 1u;
  const cap_subject_id_t intruder = 2u;

  reset_world();
  /* Both subjects hold CAP_IPC_RECV via cap_handle, but only one owns
   * the port. The handle-gated recv must reject the non-owner. */
  cap_handle_t intruder_recv = cap_handle_grant(intruder, CAP_IPC_RECV);
  if (intruder_recv == CAP_HANDLE_NULL) {
    die("handle_grant_intruder_recv");
  }

  ipc_port_t port = IPC_PORT_INVALID;
  if (ipc_port_create(port_owner, CAP_IPC_SEND, CAP_IPC_RECV, &port) != IPC_OK) {
    die("port_create_owner_check");
  }

  ipc_msg_v0 in;
  memset(&in, 0, sizeof(in));
  ipc_result_t r = ipc_recv_h(intruder_recv, port, &in);
  if (r != IPC_ERR_CAP_DENIED) {
    die("wrong_owner_not_denied");
  }
  printf("TEST:PASS:ipc_handle_gate_deny_wrong_owner\n");
}

static void test_owner_accessor_stale_returns_zero(void) {
  /* M1-CAPTBL-006 extends cap_handle.h with cap_handle_owner; the spec
   * requires it returns 0 for a stale handle. Cover here so the
   * helper carries its own contract test. */
  reset_world();
  cap_handle_t h = cap_handle_grant(7u, CAP_IPC_SEND);
  if (h == CAP_HANDLE_NULL) {
    die("owner_accessor_grant");
  }
  if (cap_handle_owner(h) != 7u) {
    die("owner_accessor_live_mismatch");
  }
  if (cap_handle_revoke(h) != CAP_OK) {
    die("owner_accessor_revoke");
  }
  if (cap_handle_owner(h) != 0u) {
    die("owner_accessor_stale_not_zero");
  }
  printf("TEST:PASS:ipc_handle_gate_owner_accessor\n");
}

int main(void) {
  test_owner_accessor_stale_returns_zero();
  test_allow();
  test_deny_wrong_cap();
  test_deny_stale();
  test_deny_wrong_owner_on_recv();
  printf("TEST:PASS:ipc_handle_gate\n");
  return 0;
}
