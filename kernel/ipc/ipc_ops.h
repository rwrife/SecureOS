/**
 * @file ipc_ops.h
 * @brief Public entry points for the M1 synchronous IPC primitive (v0).
 *
 * Purpose:
 *   Declares the three synchronous IPC ops (`ipc_send`, `ipc_recv`,
 *   `ipc_call`) implemented in `ipc_ops.c`. These are the v0 surface
 *   the rest of the kernel (and the host-side scaffolding test) call
 *   into. Wire layout and error vocabulary live in `ipc_msg.h`; port
 *   table lifecycle lives in `ipc_port.h`.
 *
 * Interactions:
 *   - kernel/ipc/ipc_msg.h: envelope and `ipc_result_t`.
 *   - kernel/ipc/ipc_port.h: handle resolution + single-waiter slot.
 *   - kernel/cap/capability.h: `cap_subject_id_t` plus the
 *     CAP_IPC_SEND / CAP_IPC_RECV capabilities consulted by the gate.
 *
 * Launched by:
 *   Not a standalone process. Header-only declarations.
 *
 * Issue: #210.
 */

#ifndef SECUREOS_KERNEL_IPC_OPS_H
#define SECUREOS_KERNEL_IPC_OPS_H

#include "ipc_msg.h"
#include "ipc_port.h"
#include "../cap/capability.h"
#include "../cap/cap_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Synchronous send: stage `*msg` into `target`'s single-waiter slot
 * after checking that `sender` holds the port's send_cap. On success
 * the staged envelope's `sender_subject` field is the kernel-stamped
 * value of `sender`, not the caller-supplied one (spec §2.4).
 *
 * Returns:
 *   IPC_OK              — envelope staged.
 *   IPC_ERR_INVALID_PORT — handle stale or unknown.
 *   IPC_ERR_INVALID_MSG  — envelope rejected by validation
 *                          (abi_version mismatch, flags != 0,
 *                          payload_len > IPC_MSG_PAYLOAD_MAX,
 *                          sender == 0).
 *   IPC_ERR_CAP_DENIED   — sender lacks the port's send_cap; a
 *                          CAP:DENY marker has been emitted.
 *   IPC_ERR_PEER_GONE    — slot already occupied (v0 has no wait
 *                          queue; see ipc-wire.md §1).
 */
ipc_result_t ipc_send(cap_subject_id_t sender,
                      ipc_port_t target,
                      const ipc_msg_v0 *msg);

/*
 * Synchronous receive: consume the staged envelope on `self_port` into
 * `*out_msg` after asserting that `receiver` owns the port and holds
 * the port's recv_cap. Returns IPC_ERR_CAP_DENIED (with a CAP:DENY
 * marker) if either check fails.
 */
ipc_result_t ipc_recv(cap_subject_id_t receiver,
                      ipc_port_t self_port,
                      ipc_msg_v0 *out_msg);

/*
 * Synchronous request/reply: send `*req` to `target` then receive on a
 * caller-owned `reply_port`. The reply-port handle is carried by the
 * caller in `req->tag` for forward-compat with the §2.3 reply-port
 * encoding; in v0 the handle is also passed explicitly to make the
 * ownership check unambiguous.
 */
ipc_result_t ipc_call(cap_subject_id_t caller,
                      ipc_port_t target,
                      const ipc_msg_v0 *req,
                      ipc_port_t reply_port,
                      ipc_msg_v0 *out_reply);

/* --------------------------------------------------------------------
 * M1-CAPTBL-006 (issue #246): handle-gated IPC entry points.
 *
 * These sit alongside the subject-keyed `ipc_send` / `ipc_recv` above
 * and route the capability decision through
 * `cap_gate_check_handle(handle, required_cap)` instead of
 * `cap_check(subject, required_cap)`. The handle's row also supplies
 * the authenticated subject, so the caller does not get to spoof it.
 *
 * Same wire format, same deny vocabulary, same audit ring. The legacy
 * `ipc_send` / `ipc_recv` paths are left intact so existing tests pass
 * unmodified (plan #197 acceptance \#2).
 * --------------------------------------------------------------------*/

/*
 * Handle-gated send. The kernel-trusted sender id is derived from
 * `send_handle` (via cap_handle_owner). `send_handle` must resolve to a
 * live row whose cap_id matches the port's send_cap; otherwise the call
 * fails with IPC_ERR_CAP_DENIED and emits the canonical
 * CAP:DENY:<owner>:<send_cap_name>:- marker.
 */
ipc_result_t ipc_send_h(cap_handle_t send_handle,
                        ipc_port_t target,
                        const ipc_msg_v0 *msg);

/*
 * Handle-gated receive. Mirrors ipc_recv but takes a `recv_handle`
 * whose owner must match the port owner, and whose cap_id must match
 * the port's recv_cap. Wrong-owner-on-recv is treated as a CAP_IPC_RECV
 * deny (deny marker emitted) so the policy-leakage surface is identical
 * to the legacy path.
 */
ipc_result_t ipc_recv_h(cap_handle_t recv_handle,
                        ipc_port_t self_port,
                        ipc_msg_v0 *out_msg);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_IPC_OPS_H */
