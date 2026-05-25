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

#include "../cap/capability.h"
#include "../ipc/ipc_port.h"

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
  BROKER_OP_INVALID = 0,
  BROKER_OP_REQUEST = 1,
  BROKER_OP_APPROVE = 2,
  BROKER_OP_DENY    = 3,
} broker_op_t;

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_SVC_BROKER_SVC_H */
