/**
 * @file console_svc.h
 * @brief M2-on-M1 console service: kernel-side IPC endpoint module.
 *
 * Purpose:
 *   Allocates one well-known IPC port via `kernel/ipc/ipc_port.c` at
 *   subsystem init time. The port is owned by the canonical console
 *   subject (`SUBJECT_M2_CONSOLE_SVC`, see `tests/harness/m2_subjects.h`)
 *   and is gated by `CAP_CONSOLE_WRITE` for both directions in the v0
 *   slice — the recv side is additionally gated by the port-owner check
 *   in `ipc_recv`, so no new capability id is introduced.
 *
 *   This is **slice 1 of plan #263 (M2-on-M1 substrate, issue #259)**.
 *   Subsequent slices wire HelloApp (#270), launcher manifest handoff
 *   (#269), and the `_qemu` peers (#271) on top of this module.
 *
 *   Out of scope for slice 1:
 *     - Driving an actual recv loop / forwarding bytes to the existing
 *       console driver (`kernel/core/console.c`) — slice 3 / #270.
 *     - PCB allocation for the service itself (`process_create` call)
 *       — slice 4 (`_qemu` peers) once the boot sequence is wired in.
 *     - Any user-visible ABI surface — see "ABI" below.
 *
 * ABI:
 *   Internal-only. This header is not part of the frozen `docs/abi/`
 *   surface; the module exposes its port handle to in-kernel callers
 *   (launcher, registry walkers) only. The on-wire envelope continues
 *   to be the canonical `ipc_msg_v0` defined by `kernel/ipc/ipc_msg.h`.
 *
 * Interactions:
 *   - kernel/ipc/ipc_port.{c,h}: port table; this module is one client.
 *   - kernel/cap/capability.h: `CAP_CONSOLE_WRITE` send/recv gate.
 *   - tests/harness/m2_subjects.h: canonical subject ids.
 *
 * Launched by:
 *   `console_svc_init()` is called by `kernel/core/kmain.c` at boot
 *   between `ipc_port_table_init()` and `proc_init()` (boot-order edge
 *   documented in the implementation). In host tests, the test driver
 *   calls it directly after `ipc_port_table_reset()`.
 *
 * Issue: #268. Plan: plans/2026-05-23-m2-on-m1-substrate.md slice 1.
 */

#ifndef SECUREOS_KERNEL_SVC_CONSOLE_SVC_H
#define SECUREOS_KERNEL_SVC_CONSOLE_SVC_H

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
 */
typedef enum {
  CONSOLE_SVC_OK = 0,
  CONSOLE_SVC_ERR_PORT_ALLOC = 1,
  CONSOLE_SVC_ERR_ALREADY_INIT = 2,
} console_svc_result_t;

/*
 * Allocate the well-known console-service port. Idempotent-by-error:
 * a second call before `console_svc_reset()` returns
 * `CONSOLE_SVC_ERR_ALREADY_INIT` and does not allocate a second port.
 *
 * On success, the allocated port:
 *   - is owned by `SUBJECT_M2_CONSOLE_SVC`,
 *   - has `send_cap = CAP_CONSOLE_WRITE`,
 *   - has `recv_cap = CAP_CONSOLE_WRITE` (recv-side gated by owner
 *     check; see the plan §"Console service module" for rationale).
 *
 * The port handle is retrievable via `console_svc_port()` until reset.
 */
console_svc_result_t console_svc_init(void);

/*
 * Tear down the in-memory state. Does NOT call `ipc_port_destroy()` on
 * the underlying handle — callers that want the port released must
 * either `ipc_port_table_reset()` (test harness) or land slice 4's
 * destroy wiring. Always safe to call; no-op when not initialised.
 *
 * Provided so test setup/teardown can run in any order without leaking
 * state across phases.
 */
void console_svc_reset(void);

/*
 * Return the port handle allocated by `console_svc_init()`. Returns
 * `IPC_PORT_INVALID` when the service has not been initialised. The
 * launcher (slice 2, #269) calls this when minting handles for a
 * spawned app.
 */
ipc_port_t console_svc_port(void);

/*
 * Return true iff `console_svc_init()` has been called and not yet
 * reset. Exposed so the validator test can assert exact init/teardown
 * lifecycle.
 */
bool console_svc_is_initialised(void);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_SVC_CONSOLE_SVC_H */
