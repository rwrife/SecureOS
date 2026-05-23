/**
 * @file ipc_port.h
 * @brief IPC port table for the M1 synchronous IPC primitive (v0).
 *
 * Purpose:
 *   Owns the kernel-side port table that backs `ipc_port_t` handles
 *   referenced by `kernel/ipc/ipc_ops.c`. Each port records:
 *     - the owning subject (the only subject that may receive on it),
 *     - the `send_cap` and `recv_cap` capability ids that gate
 *       interaction with the port (defaults: CAP_IPC_SEND / CAP_IPC_RECV),
 *     - a single in-flight envelope slot (the "single-waiter slot" from
 *       the issue scope) used to emulate synchronous rendezvous in the
 *       v0 in-kernel-only model.
 *
 *   Handles are 32-bit opaque integers `(generation << 16) | index`.
 *   The generation counter is bumped on destroy so stale handles fail
 *   with IPC_ERR_INVALID_PORT rather than aliasing a recycled slot.
 *
 * Interactions:
 *   - kernel/ipc/ipc_ops.c: drives the staging/consume cycle and is the
 *     only writer of the staged envelope's `sender_subject` field.
 *   - kernel/cap/capability.h: the port's send_cap/recv_cap fields are
 *     `capability_id_t` values.
 *
 * Launched by:
 *   Not a standalone process. `ipc_port_table_reset()` is called by
 *   tests at start-of-run and (when the kernel boots) from kmain at
 *   subsystem init time.
 *
 * Issue: #210. Plan: plans/2026-05-19-m1-sync-ipc-primitive.md.
 */

#ifndef SECUREOS_KERNEL_IPC_PORT_H
#define SECUREOS_KERNEL_IPC_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../cap/capability.h"
#include "ipc_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maximum simultaneously live ports in the v0 in-kernel scaffold. Sized
 * deliberately small — anything that needs more is out of scope for the
 * session-sized M1 slice.
 */
#define IPC_PORT_TABLE_MAX 16u

/*
 * Reserved invalid handle. ipc_port_create() never returns this value
 * on success. Tests use it to assert default-zero handles are rejected.
 */
#define IPC_PORT_INVALID 0u

typedef uint32_t ipc_port_t;

/*
 * Initialize / reset the port table to a known-empty state. Both forms
 * are idempotent. Calling reset invalidates every previously issued
 * handle (their generations are bumped by the reset).
 */
void ipc_port_table_init(void);
void ipc_port_table_reset(void);

/*
 * Create a port owned by `owner` requiring `send_cap` for ipc_send and
 * `recv_cap` for ipc_recv. Returns IPC_OK and writes the new handle to
 * `*out_port` on success; returns IPC_ERR_INVALID_PORT on table full or
 * IPC_ERR_INVALID_MSG if any input is invalid.
 */
ipc_result_t ipc_port_create(cap_subject_id_t owner,
                             capability_id_t send_cap,
                             capability_id_t recv_cap,
                             ipc_port_t *out_port);

/*
 * Destroy a port. Any envelope staged in its single-waiter slot is
 * dropped. Subsequent operations on the same handle return
 * IPC_ERR_INVALID_PORT (generation mismatch).
 */
ipc_result_t ipc_port_destroy(ipc_port_t port);

/*
 * Read-only accessors used by ipc_ops.c to consult the port's owner /
 * capability requirements without exposing the underlying slot
 * representation. Each returns IPC_ERR_INVALID_PORT for a stale handle.
 */
ipc_result_t ipc_port_owner(ipc_port_t port, cap_subject_id_t *out_owner);
ipc_result_t ipc_port_send_cap(ipc_port_t port, capability_id_t *out_cap);
ipc_result_t ipc_port_recv_cap(ipc_port_t port, capability_id_t *out_cap);

/*
 * Single-waiter slot operations. v0 emulates the "blocking rendezvous"
 * spec by ordering: a sender stages an envelope into an empty slot and
 * a receiver later drains it. The v0 in-kernel scaffold returns
 * IPC_ERR_PEER_GONE when the slot is in the wrong state for the op
 * (occupied on stage, empty on consume) — a strict scheduler-driven
 * blocking implementation is deferred (see ipc-wire.md §1 non-goals).
 */
ipc_result_t ipc_port_stage(ipc_port_t port, const ipc_msg_v0 *msg);
ipc_result_t ipc_port_consume(ipc_port_t port, ipc_msg_v0 *out_msg);

/*
 * Test-only helper: does `port` currently have a staged envelope?
 * Used by ipc_sync_v0_test.c to assert rendezvous progress without
 * peeking at slot internals.
 */
bool ipc_port_has_pending_for_tests(ipc_port_t port);

/*
 * Stable kernel-internal address for a live port. The pointer is
 * intended only as an opaque equality token shared between the IPC
 * send/recv slow path and the cooperative scheduler's block/wake
 * helpers (kernel/proc/proc_sched.{c,h}). The pointed-to bytes are
 * NOT readable by callers — do not dereference. Returns NULL for a
 * stale or invalid handle.
 *
 * Issue #250 (M1 plan #198 slice 3): wired up so the scheduler can
 * suspend a PCB on "this exact port" and the matching peer can wake
 * exactly one waiter for the same port without exposing the slot
 * representation.
 */
const void *ipc_port_wait_token(ipc_port_t port);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_IPC_PORT_H */
