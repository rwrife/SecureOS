/**
 * @file broker_svc.h
 * @brief M4-on-M1 capability-broker service: kernel-side IPC endpoint
 *        module (single port).
 *
 * Purpose:
 *   Allocates one well-known IPC port via `kernel/ipc/ipc_port.c` at
 *   subsystem init time. The port is owned by the canonical
 *   broker-svc subject (`SUBJECT_M4_BROKER_SVC`, see
 *   `tests/harness/svc_subjects.h`) and is gated by `CAP_IPC_SEND` for
 *   both directions in the v0 slice — broker authority is
 *   subject-bound (checked inside `cap_broker_*`), not cap-bound, so
 *   no new capability id is introduced. This matches "option 1" in
 *   plan `plans/2026-05-25-m4-broker-on-m1-substrate.md`
 *   §"Capability id for the broker-svc port" (the recommended
 *   default).
 *
 *   This is **slice 1 of plan #300 (M4-on-M1 substrate, issue #299)**.
 *   Subsequent slices wire launcher handoff via `ipc_scratch[24..31]`
 *   (#303), `_qemu` allow/deny acceptance peers (#304), and the
 *   `_qemu` revoke peer + `process_destroy` recycle assertion (#305)
 *   on top of this module.
 *
 *   Out of scope for slice 1 (deferred to later slices, mirroring the
 *   fs_svc / console_svc slice-1 precedent from #272 / #282):
 *     - Driving an actual recv loop / handling broker request/approve/
 *       deny/revoke envelopes — that lands with the acceptance peers
 *       (#304 / #305).
 *     - The `BROKER_OP_*` tag enum and the `share_id -> cap_handle_t`
 *       side-table — those land in the slice that introduces the
 *       op-dispatch step, alongside an envelope encoding for the
 *       broker request parameters.
 *     - Launcher → broker handle handoff via `ipc_scratch[24..31]`
 *       (#303).
 *     - PCB allocation for the service itself.
 *     - Any user-visible ABI surface — see "ABI" below. No
 *       `OS_ABI_VERSION` bump in this slice.
 *
 * ABI:
 *   Internal-only. This header is not part of the frozen `docs/abi/`
 *   surface; the module exposes its port handle to in-kernel callers
 *   (launcher, registry walkers) only. The on-wire envelope continues
 *   to be the canonical `ipc_msg_v0` defined by `kernel/ipc/ipc_msg.h`.
 *
 * Interactions:
 *   - kernel/ipc/ipc_port.{c,h}: port table; this module is one client.
 *   - kernel/cap/capability.h: `CAP_IPC_SEND` send/recv gate.
 *   - tests/harness/svc_subjects.h: canonical subject ids.
 *
 * Launched by:
 *   `broker_svc_init()` is called by `kernel/core/kmain.c` at boot on
 *   the boot-order edge extending #287:
 *     `ipc_port_table_init → console_svc_init → fs_svc_init →
 *      broker_svc_init → (proc_init)`
 *   In host tests, the test driver calls it directly after
 *   `ipc_port_table_reset()`.
 *
 * Issue: #302. Plan: plans/2026-05-25-m4-broker-on-m1-substrate.md
 * slice 1.
 */

#ifndef SECUREOS_KERNEL_SVC_BROKER_SVC_H
#define SECUREOS_KERNEL_SVC_BROKER_SVC_H

#include <stdbool.h>
#include <stdint.h>

#include "../cap/cap_broker.h"
#include "../cap/cap_handle.h"
#include "../cap/capability.h"
#include "../ipc/ipc_port.h"
#include "../proc/process.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Result vocabulary for the slice-1 init entry point. Distinct from
 * `ipc_result_t` so callers can tell module-level wiring failures
 * (already initialised, port table full) apart from IPC-level errors.
 * Mirrors `console_svc_result_t` from #272 and `fs_svc_result_t` from
 * #278.
 */
typedef enum {
  BROKER_SVC_OK = 0,
  BROKER_SVC_ERR_PORT_ALLOC = 1,
  BROKER_SVC_ERR_ALREADY_INIT = 2,
  /*
   * Authority check inside `broker_svc_delete_owner` rejected the
   * actor (M5-SUBSTRATE-002, issue #324). The canonical CAP:DENY
   * marker has already been emitted by `cap_broker_delete_owner_check`
   * when this code is returned.
   */
  BROKER_SVC_ERR_DELETE_DENIED = 3,
} broker_svc_result_t;

/*
 * Allocate the well-known broker-service port. Idempotent-by-error:
 * a second call before `broker_svc_reset()` returns
 * `BROKER_SVC_ERR_ALREADY_INIT` and does not allocate a second port.
 *
 * On success, the allocated port:
 *   - is owned by `SUBJECT_M4_BROKER_SVC`,
 *   - has `send_cap = CAP_IPC_SEND`,
 *   - has `recv_cap = CAP_IPC_SEND` (recv-side additionally gated by
 *     the port-owner check inside `ipc_recv`; the broker performs its
 *     own subject-based authority check on each op so a generic
 *     send/recv cap suffices — see plan §"Capability id for the
 *     broker-svc port" option 1).
 *
 * The port handle is retrievable via `broker_svc_port()` until reset.
 */
broker_svc_result_t broker_svc_init(void);

/*
 * Tear down the in-memory state. Does NOT call `ipc_port_destroy()` on
 * the underlying handle — callers that want the port released must
 * either `ipc_port_table_reset()` (test harness) or land the destroy
 * wiring in a future slice. Always safe to call; no-op when not
 * initialised. Same convention as `console_svc_reset()` /
 * `fs_svc_reset()`.
 */
void broker_svc_reset(void);

/*
 * Return the port handle allocated by `broker_svc_init()`. Returns
 * `IPC_PORT_INVALID` when the service has not been initialised. The
 * launcher (slice 2, #303) calls this when minting a handle for a
 * spawned app via `ipc_scratch[24..31]`.
 */
ipc_port_t broker_svc_port(void);

/*
 * Return true iff `broker_svc_init()` has been called and not yet
 * reset. Exposed so the validator test can assert exact init/teardown
 * lifecycle.
 */
bool broker_svc_is_initialised(void);

/* ----------------------------------------------------------------
 * Broker IPC envelope op vocabulary (slice 3 of plan #300, issue
 * #304). The owner / recipient peers carry the op tag in
 * `ipc_msg_v0::tag` and the op-specific parameters in the 64-byte
 * payload region per the schemas below. The on-wire envelope itself
 * remains the canonical `ipc_msg_v0` — no new ABI surface is
 * introduced; `tag` is documented as caller-defined by
 * `kernel/ipc/ipc_msg.h`.
 *
 * Payload schemas (all multi-byte fields little-endian; reserved
 * bytes MUST be zero):
 *
 *   BROKER_OP_REQUEST  ─ owner → broker
 *     offset  size  field
 *          0     4  recipient_subject_id  (cap_subject_id_t)
 *          4     4  capability_id         (capability_id_t)
 *          8     1  resource_name_len     (0..31)
 *          9    31  resource_name         (NUL-padded; not required
 *                                          NUL-terminated, len drives)
 *         40    24  reserved (MBZ)
 *
 *   BROKER_OP_APPROVE  ─ owner → broker
 *     offset  size  field
 *          0     4  share_id              (cap_share_id_t)
 *          4    60  reserved (MBZ)
 *
 *   BROKER_OP_DENY     ─ owner → broker
 *     same layout as BROKER_OP_APPROVE
 *
 * The slice-3 test driver acts as the broker recv loop (mirroring
 * how M2/M3 test drivers act as the console/fs recv loops) and
 * fans the parsed envelopes into `cap_broker_*`. A future slice
 * may move the dispatch in-kernel; the on-wire schema above is
 * the contract that boundary will follow.
 *
 * `tag` values are namespaced under the BROKER_OP_* prefix so they
 * cannot collide with future fs/console op tags. The numeric values
 * are stable for v0 — do not renumber without an ABI bump.
 */
typedef enum {
  BROKER_OP_INVALID      = 0,
  BROKER_OP_REQUEST      = 1,
  BROKER_OP_APPROVE      = 2,
  BROKER_OP_DENY         = 3,
  BROKER_OP_REVOKE       = 4,
  /*
   * BROKER_OP_DELETE_OWNER (M5-SUBSTRATE-002, issue #324). Owner →
   * broker request to cascade-delete an owner subject. Payload:
   *   offset  size  field
   *        0     4  owner_subject_id  (cap_subject_id_t)
   *        4    60  reserved (MBZ)
   * The broker runs the six-step cascade documented at
   * `broker_svc_delete_owner()`; the reply tag echoes
   * BROKER_OP_DELETE_OWNER with status + cascade count.
   */
  BROKER_OP_DELETE_OWNER = 5,
} broker_op_t;

/* ----------------------------------------------------------------
 * M5-SUBSTRATE-002 (issue #324): cascade-deletion entry points.
 *
 * The broker module owns a small `share_id -> cap_handle_t`
 * side-table (bounded at `CAP_BROKER_MAX_SHARES`, mirroring
 * `kernel/cap/cap_broker.c`'s share table) so it can later walk
 * every minted-child handle that descends from a given owner's
 * broker-port handle and feed the root into
 * `cap_handle_revoke_subtree` (M5-SUBSTRATE-001, #323).
 *
 * No `OS_ABI_VERSION` bump: this surface is in-kernel only and
 * `BROKER_OP_DELETE_OWNER` is a private dispatch tag in the
 * `ipc_msg_v0::tag` namespace already documented as caller-defined.
 * ---------------------------------------------------------------- */

/*
 * Approve a previously-requested share AND mint a recipient-side
 * capability handle linked to the owner's broker-port handle
 * (`owner_broker_handle`) via `cap_handle_grant_child`, so the
 * minted row becomes reachable from the slice-001 subtree walker.
 *
 * Semantics:
 *   1. Calls `cap_broker_approve(approver, share_id)` (state
 *      transition + recipient cap_table grant + audit emission).
 *   2. On CAP_BROKER_OK, mints
 *      `cap_handle_grant_child(recipient_subject, capability_id,
 *                              owner_broker_handle)`. The caller
 *      supplies (recipient, cap) explicitly because cap_broker's
 *      share table is private to that module — the test driver /
 *      future in-kernel dispatcher already parses these fields from
 *      the on-wire BROKER_OP_APPROVE envelope per the slice-3 schema
 *      documented above.
 *   3. Records `(share_id, approver, recipient, child_handle)` in
 *      the broker_svc side-table for the cascade walker.
 *
 *   `owner_broker_handle == CAP_HANDLE_NULL` is legal — the row is
 *   then sentinel-parented (same semantics as the legacy
 *   `cap_handle_grant` forwarder) and will not be swept by an
 *   unrelated cascade. `out_recipient_handle` may be NULL.
 *
 * Returns CAP_BROKER_OK on success; otherwise the failure code from
 * `cap_broker_approve` (or CAP_BROKER_ERR_INVALID_CAPABILITY if the
 * cap-handle layer refused to mint a child row, e.g. table full).
 */
cap_broker_result_t broker_svc_approve_h(cap_subject_id_t approver_subject_id,
                                         cap_share_id_t   share_id,
                                         cap_subject_id_t recipient_subject_id,
                                         capability_id_t  capability_id,
                                         cap_handle_t     owner_broker_handle,
                                         cap_handle_t    *out_recipient_handle);

/*
 * Authority predicate for `BROKER_OP_DELETE_OWNER`. Returns 1 when
 * `actor` is allowed to cascade-delete `owner`, 0 otherwise.
 *
 * Policy (M5 v0):
 *   - actor == owner            -> ALLOW (self-delete).
 *   - actor == SUBJECT_M5_ADMIN -> ALLOW (admin override, currently a
 *                                 SKIP-shape stub — see plan §"Admin
 *                                 gate"; will be folded into a real
 *                                 CAP-013 check when that capability
 *                                 lands).
 *   - everything else           -> DENY. The function emits exactly
 *                                  one canonical CAP:DENY:<actor>:
 *                                  capability_admin:delete_owner_<id>
 *                                  marker line through
 *                                  cap_deny_marker_format (#221/#244
 *                                  conformance grammar). The
 *                                  capability_id field reuses
 *                                  CAP_CAPABILITY_ADMIN because no
 *                                  `cap_broker_delete_owner` enum
 *                                  value exists and the issue
 *                                  forbids an ABI bump; the resource
 *                                  field encodes the intent. Same
 *                                  marker-reuse pattern as
 *                                  kernel/proc/process.c's
 *                                  `proc_table_full` deny.
 */
int cap_broker_delete_owner_check(cap_subject_id_t actor_subject_id,
                                  cap_subject_id_t owner_subject_id);

/*
 * Run the six-step `BROKER_OP_DELETE_OWNER` cascade for `owner`:
 *
 *   1. `cap_broker_delete_owner_check(actor, owner)` — authority gate.
 *      On deny: return BROKER_SVC_ERR_DELETE_DENIED (the predicate
 *      already emitted the CAP:DENY marker).
 *   2. Walk the broker_svc side-table; for every recorded share
 *      whose owner subject matches, log a `cap.revoked.cascade`
 *      audit event (SKIP today — gated on #98).
 *   3. `cap_handle_revoke_subtree(owner_broker_handle)` — the
 *      load-bearing call that cascades through every recipient row
 *      minted via `broker_svc_approve`.
 *   3.5. WM session teardown (M5-SUBSTRATE-005b, issue #350) — drain
 *      every window-manager session owned by `owner_subject_id` via
 *      `session_manager_first_session_for_subject` +
 *      `session_manager_destroy`. Audit emission SKIP today (gated
 *      on #98, additive `child_kind=SESSION` value per plan
 *      `plans/2026-05-26-m5-wm-cascade-on-substrate.md`). Each
 *      destroyed session adds one to `*out_n`. Ordering: AFTER
 *      step 3 (so torn-down sessions cannot mint fresh shares),
 *      BEFORE step 4 (so per-session console context unbinds before
 *      its PCB goes away).
 *   4. `process_destroy(owner_pid)` if `owner_pid != PID_INVALID`.
 *      No-op when the caller doesn't yet have a PCB wired (the
 *      side-table cascade is the binding effect in that case).
 *   5. Emit summary `cap.cascade.done {owner, n_children}` audit
 *      event (SKIP today).
 *   6. Write the cascade total (side-table entries swept + WM
 *      sessions destroyed) into `*out_n` and return BROKER_SVC_OK.
 *
 * `out_n` may be NULL; in that case the count is discarded.
 */
broker_svc_result_t broker_svc_delete_owner(cap_subject_id_t actor_subject_id,
                                            cap_subject_id_t owner_subject_id,
                                            cap_handle_t owner_broker_handle,
                                            process_id_t owner_pid,
                                            uint32_t *out_n);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_SVC_BROKER_SVC_H */
