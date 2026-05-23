/**
 * @file ipc_ops.c
 * @brief Synchronous IPC primitive ops (send/recv/call) for v0 (#210).
 *
 * Purpose:
 *   Implements the three synchronous IPC operations defined in
 *   `docs/abi/ipc-wire.md` §4 on top of the port table in
 *   `kernel/ipc/ipc_port.{c,h}`:
 *     - ipc_send  : capability-checked stage into a port's slot.
 *     - ipc_recv  : capability-checked + ownership-checked consume from
 *                   a port's slot.
 *     - ipc_call  : send + recv round-trip over a caller-owned reply
 *                   port whose handle is carried in `req->tag`.
 *
 *   The deny path threads through cap_check() (so the existing audit
 *   ring records every check unchanged) and emits the canonical
 *   `CAP:DENY:<subject>:<cap>:-` marker required by
 *   `docs/abi/capability-deny-contract.md` before returning
 *   IPC_ERR_CAP_DENIED.
 *
 *   In v0 every op is in-kernel-only between two test modules; there is
 *   no scheduler suspension, so the "blocks until rendezvous" wording
 *   in the spec is satisfied by the ordering invariants of the test
 *   harness rather than a true wait queue. The wire layout and error
 *   model are the binding ABI contract; the in-kernel mechanism behind
 *   them is intentionally minimal and replaceable.
 *
 * Interactions:
 *   - kernel/ipc/ipc_port.{c,h}: port table, single-waiter slot.
 *   - kernel/ipc/ipc_msg.h:      envelope + ipc_result_t vocabulary.
 *   - kernel/cap/capability.{c,h}: cap_check() drives the audit ring.
 *
 * Launched by:
 *   Not a standalone process. Compiled into the kernel image and into
 *   the host-side ipc_sync_v0 test binary.
 *
 * Issue: #210.
 */

#include "ipc_msg.h"
#include "ipc_port.h"
#include "../cap/capability.h"
#include "../cap/cap_handle.h"
#include "../proc/proc_sched.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------
 * Slice-3 (#250) block/wake helpers.
 *
 * When the cooperative scheduler is driving execution, an occupied
 * single-waiter slot on send (or an empty slot on recv) must suspend
 * the current PCB until the peer half of the rendezvous arrives.
 *
 * When NO scheduler is active (the pre-#250 in-kernel-only test
 * harness shape), we fall through to the v0 ordering-driven semantics:
 * stage_on_full -> IPC_ERR_PEER_GONE and consume_on_empty ->
 * IPC_ERR_PEER_GONE. This keeps ipc_sync_v0 / ipc_handle_gate /
 * ipc_port_lifecycle green without changes.
 * ------------------------------------------------------------------ */
static ipc_result_t ipc_stage_blocking(ipc_port_t target, const ipc_msg_v0 *outbound) {
  for (;;) {
    ipc_result_t sr = ipc_port_stage(target, outbound);
    if (sr != IPC_ERR_PEER_GONE || !proc_sched_is_active()) {
      if (sr == IPC_OK) {
        const void *tok = ipc_port_wait_token(target);
        if (tok != NULL) {
          (void)proc_sched_wake_one_on_port(tok);
        }
      }
      return sr;
    }
    const void *tok = ipc_port_wait_token(target);
    if (tok == NULL) {
      return IPC_ERR_INVALID_PORT;
    }
    proc_sched_result_t br = proc_sched_block_current_on_port(tok);
    if (br != PROC_SCHED_OK) {
      return IPC_ERR_PEER_GONE;
    }
  }
}

static ipc_result_t ipc_consume_blocking(ipc_port_t self_port, ipc_msg_v0 *out_msg) {
  for (;;) {
    ipc_result_t cr = ipc_port_consume(self_port, out_msg);
    if (cr != IPC_ERR_PEER_GONE || !proc_sched_is_active()) {
      if (cr == IPC_OK) {
        const void *tok = ipc_port_wait_token(self_port);
        if (tok != NULL) {
          (void)proc_sched_wake_one_on_port(tok);
        }
      }
      return cr;
    }
    const void *tok = ipc_port_wait_token(self_port);
    if (tok == NULL) {
      return IPC_ERR_INVALID_PORT;
    }
    proc_sched_result_t br = proc_sched_block_current_on_port(tok);
    if (br != PROC_SCHED_OK) {
      return IPC_ERR_PEER_GONE;
    }
  }
}

/* Spec-mandated canonical name for the CAP:DENY marker resource field
 * when the operation has no resource handle of its own
 * (capability-deny-contract.md §4). */
#define IPC_DENY_RESOURCE_NONE "-"

static const char *ipc_cap_name(capability_id_t cap) {
  switch (cap) {
    case CAP_IPC_SEND: return "ipc_send";
    case CAP_IPC_RECV: return "ipc_recv";
    case CAP_CONSOLE_WRITE: return "console_write";
    case CAP_SERIAL_WRITE: return "serial_write";
    case CAP_DEBUG_EXIT: return "debug_exit";
    case CAP_CAPABILITY_ADMIN: return "capability_admin";
    case CAP_DISK_IO_REQUEST: return "disk_io_request";
    case CAP_FS_READ: return "fs_read";
    case CAP_FS_WRITE: return "fs_write";
    case CAP_EVENT_SUBSCRIBE: return "event_subscribe";
    case CAP_EVENT_PUBLISH: return "event_publish";
    case CAP_APP_EXEC: return "app_exec";
    case CAP_CODESIGN_BYPASS: return "codesign_bypass";
    case CAP_NETWORK: return "network";
    case CAP_SYSCALL: return "syscall";
  }
  return "unknown";
}

static void ipc_emit_deny_marker(cap_subject_id_t subject, capability_id_t cap) {
  /* Canonical: CAP:DENY:<actor_subject_id>:<cap_id_name>:<resource>
   * IPC has no per-call resource handle in v0, so the resource is '-'. */
  printf("CAP:DENY:%u:%s:%s\n", (unsigned)subject, ipc_cap_name(cap), IPC_DENY_RESOURCE_NONE);
}

/* Capability-gate helpers. These intentionally mirror the shape of
 * cap_gate.c so audit-side behavior is identical to other gated paths. */
static ipc_result_t cap_ipc_send_gate(cap_subject_id_t subject, capability_id_t required) {
  cap_result_t r = cap_check(subject, required);
  if (r != CAP_OK) {
    ipc_emit_deny_marker(subject, required);
    return IPC_ERR_CAP_DENIED;
  }
  return IPC_OK;
}

static ipc_result_t cap_ipc_recv_gate(cap_subject_id_t subject, capability_id_t required) {
  cap_result_t r = cap_check(subject, required);
  if (r != CAP_OK) {
    ipc_emit_deny_marker(subject, required);
    return IPC_ERR_CAP_DENIED;
  }
  return IPC_OK;
}

static ipc_result_t validate_outbound_envelope(const ipc_msg_v0 *msg) {
  if (msg == NULL) {
    return IPC_ERR_INVALID_MSG;
  }
  if (msg->abi_version != (uint16_t)OS_ABI_VERSION) {
    return IPC_ERR_INVALID_MSG;
  }
  if (msg->flags != 0u) {
    return IPC_ERR_INVALID_MSG;
  }
  if (msg->payload_len > IPC_MSG_PAYLOAD_MAX) {
    return IPC_ERR_INVALID_MSG;
  }
  return IPC_OK;
}

ipc_result_t ipc_send(cap_subject_id_t sender, ipc_port_t target, const ipc_msg_v0 *msg) {
  ipc_result_t v = validate_outbound_envelope(msg);
  if (v != IPC_OK) {
    return v;
  }

  capability_id_t required_send = CAP_IPC_SEND;
  ipc_result_t pr = ipc_port_send_cap(target, &required_send);
  if (pr != IPC_OK) {
    return pr;
  }

  ipc_result_t gate = cap_ipc_send_gate(sender, required_send);
  if (gate != IPC_OK) {
    return gate;
  }

  /* Stamp authenticated sender id; spec §2.4 forbids trusting the
   * caller-supplied value. A delivered envelope with sender_subject == 0
   * is treated as IPC_ERR_INVALID_MSG by the receiver. */
  if (sender == 0u) {
    return IPC_ERR_INVALID_MSG;
  }

  ipc_msg_v0 outbound;
  memcpy(&outbound, msg, sizeof(outbound));
  outbound.sender_subject = sender;

  return ipc_stage_blocking(target, &outbound);
}

ipc_result_t ipc_recv(cap_subject_id_t receiver, ipc_port_t self_port, ipc_msg_v0 *out_msg) {
  if (out_msg == NULL) {
    return IPC_ERR_INVALID_MSG;
  }

  cap_subject_id_t owner = 0u;
  ipc_result_t pr = ipc_port_owner(self_port, &owner);
  if (pr != IPC_OK) {
    return pr;
  }
  if (owner != receiver) {
    /* Spec §3: only the owning subject may receive on a port. Treat
     * as a capability decision so policy-leakage tests can rely on
     * the single deny code. */
    ipc_emit_deny_marker(receiver, CAP_IPC_RECV);
    return IPC_ERR_CAP_DENIED;
  }

  capability_id_t required_recv = CAP_IPC_RECV;
  pr = ipc_port_recv_cap(self_port, &required_recv);
  if (pr != IPC_OK) {
    return pr;
  }

  ipc_result_t gate = cap_ipc_recv_gate(receiver, required_recv);
  if (gate != IPC_OK) {
    return gate;
  }

  ipc_result_t cr = ipc_consume_blocking(self_port, out_msg);
  if (cr != IPC_OK) {
    return cr;
  }

  /* §2.4: a delivered envelope with sender_subject == 0 is malformed. */
  if (out_msg->sender_subject == 0u) {
    return IPC_ERR_INVALID_MSG;
  }
  if (out_msg->abi_version != (uint16_t)OS_ABI_VERSION) {
    return IPC_ERR_INVALID_MSG;
  }
  if (out_msg->flags != 0u) {
    return IPC_ERR_INVALID_MSG;
  }
  if (out_msg->payload_len > IPC_MSG_PAYLOAD_MAX) {
    return IPC_ERR_INVALID_MSG;
  }
  return IPC_OK;
}

ipc_result_t ipc_call(cap_subject_id_t caller,
                      ipc_port_t target,
                      const ipc_msg_v0 *req,
                      ipc_port_t reply_port,
                      ipc_msg_v0 *out_reply) {
  if (req == NULL || out_reply == NULL) {
    return IPC_ERR_INVALID_MSG;
  }

  /* Caller must own the reply port (§4: "owns the reply port"). The
   * reply-port handle is reserved for embedding in `req->tag` (§2.3);
   * v0 leaves the encoding caller-opaque, so we accept it as an
   * explicit argument here and the test asserts it is also carried in
   * req->tag for forward-compat. */
  cap_subject_id_t reply_owner = 0u;
  ipc_result_t pr = ipc_port_owner(reply_port, &reply_owner);
  if (pr != IPC_OK) {
    return pr;
  }
  if (reply_owner != caller) {
    ipc_emit_deny_marker(caller, CAP_IPC_RECV);
    return IPC_ERR_CAP_DENIED;
  }

  ipc_result_t sr = ipc_send(caller, target, req);
  if (sr != IPC_OK) {
    return sr;
  }

  return ipc_recv(caller, reply_port, out_reply);
}

/* --------------------------------------------------------------------
 * M1-CAPTBL-006 (issue #246): handle-gated send/recv.
 *
 * These re-use the same envelope validation, deny-marker emission, and
 * cap_check audit-ring path as the subject-keyed legacy entry points,
 * but the authoritative subject + capability decision both come from
 * `cap_handle_t` instead of trusting the caller-supplied subject id.
 *
 * On success the staged envelope's sender_subject is the kernel-trusted
 * owner from the handle's row — not the caller-supplied value. This is
 * the M1 ABI obligation from docs/abi/ipc-wire.md §2.4 made enforceable
 * without relying on test-harness discipline.
 * --------------------------------------------------------------------*/
ipc_result_t ipc_send_h(cap_handle_t send_handle,
                        ipc_port_t target,
                        const ipc_msg_v0 *msg) {
  ipc_result_t v = validate_outbound_envelope(msg);
  if (v != IPC_OK) {
    return v;
  }

  capability_id_t required_send = CAP_IPC_SEND;
  ipc_result_t pr = ipc_port_send_cap(target, &required_send);
  if (pr != IPC_OK) {
    return pr;
  }

  /* Recover the kernel-trusted sender id BEFORE the gate check so the
   * deny marker carries the same subject the audit ring will record.
   * A malformed/stale handle has owner == 0; we still want to gate on
   * a real subject id, so fall back to subject 0 and let the cap_check
   * deny path handle it (its audit event will record actor_subject=0
   * which is the canonical "unknown subject" marker). */
  cap_subject_id_t sender = cap_handle_owner(send_handle);

  /* Handle-keyed cap decision. */
  cap_result_t hr = cap_gate_check_handle_result(send_handle, required_send);
  if (hr != CAP_OK) {
    /* Mirror the legacy path: drive the audit ring via cap_check so
     * the existing CAP_AUDIT fixture diff (#243) stays byte-identical
     * for the gated-deny case, then emit the canonical marker. */
    (void)cap_check(sender, required_send);
    ipc_emit_deny_marker(sender, required_send);
    return IPC_ERR_CAP_DENIED;
  }

  /* Allow-path audit parity: also drive cap_check so the audit ring is
   * indistinguishable from a legacy ipc_send call. The bitset table is
   * managed by the cap_table façade today (M1-CAPTBL-005), so this
   * second check is a pure ring side effect. */
  (void)cap_check(sender, required_send);

  /* sender == 0 should be impossible after a CAP_OK handle check
   * (cap_handle_grant rejects subject 0 via cap_handle_subject_valid),
   * but defend in depth: treat it as a malformed envelope per spec. */
  if (sender == 0u) {
    return IPC_ERR_INVALID_MSG;
  }

  ipc_msg_v0 outbound;
  memcpy(&outbound, msg, sizeof(outbound));
  outbound.sender_subject = sender;

  return ipc_stage_blocking(target, &outbound);
}

ipc_result_t ipc_recv_h(cap_handle_t recv_handle,
                        ipc_port_t self_port,
                        ipc_msg_v0 *out_msg) {
  if (out_msg == NULL) {
    return IPC_ERR_INVALID_MSG;
  }

  cap_subject_id_t owner = 0u;
  ipc_result_t pr = ipc_port_owner(self_port, &owner);
  if (pr != IPC_OK) {
    return pr;
  }

  cap_subject_id_t receiver = cap_handle_owner(recv_handle);
  if (owner != receiver) {
    /* Wrong-owner-on-recv: treat as CAP_IPC_RECV deny so the policy
     * leakage surface matches the legacy ipc_recv path. */
    ipc_emit_deny_marker(receiver, CAP_IPC_RECV);
    return IPC_ERR_CAP_DENIED;
  }

  capability_id_t required_recv = CAP_IPC_RECV;
  pr = ipc_port_recv_cap(self_port, &required_recv);
  if (pr != IPC_OK) {
    return pr;
  }

  cap_result_t hr = cap_gate_check_handle_result(recv_handle, required_recv);
  if (hr != CAP_OK) {
    (void)cap_check(receiver, required_recv);
    ipc_emit_deny_marker(receiver, required_recv);
    return IPC_ERR_CAP_DENIED;
  }
  (void)cap_check(receiver, required_recv);

  ipc_result_t cr = ipc_consume_blocking(self_port, out_msg);
  if (cr != IPC_OK) {
    return cr;
  }

  if (out_msg->sender_subject == 0u) {
    return IPC_ERR_INVALID_MSG;
  }
  if (out_msg->abi_version != (uint16_t)OS_ABI_VERSION) {
    return IPC_ERR_INVALID_MSG;
  }
  if (out_msg->flags != 0u) {
    return IPC_ERR_INVALID_MSG;
  }
  if (out_msg->payload_len > IPC_MSG_PAYLOAD_MAX) {
    return IPC_ERR_INVALID_MSG;
  }
  return IPC_OK;
}
