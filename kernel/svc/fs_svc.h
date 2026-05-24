/**
 * @file fs_svc.h
 * @brief M3-on-M1 fs service: kernel-side IPC endpoint module
 *        (dual-port — read + write).
 *
 * Purpose:
 *   Allocates two well-known IPC ports via `kernel/ipc/ipc_port.c` at
 *   subsystem init time. Both ports are owned by the canonical fs
 *   subject (`SUBJECT_M3_FS_SVC`, see `tests/harness/svc_subjects.h`).
 *   The READ port is gated by `CAP_FS_READ` on both send/recv sides;
 *   the WRITE port is gated by `CAP_FS_WRITE`. Recv side is also
 *   gated by the port-owner check in `ipc_recv` (same convention as
 *   `console_svc`, #272).
 *
 *   This is **slice 1 of plan #277 (M3-on-M1 substrate, issue #276)**.
 *   Subsequent slices wire `launcher_fs_spawn_app_with_fs_caps` (#279),
 *   HelloApp fs-demo + persist allow/deny `_qemu` tests (#280), and
 *   the ephemeral-reset `_qemu` peer (#281) on top of this module.
 *
 *   Out of scope for slice 1:
 *     - Driving an actual recv loop / handling fs_read/fs_write
 *       envelopes — that lands with the persist tests in #280.
 *     - PCB allocation for the service itself.
 *     - Any user-visible ABI surface — see "ABI" below.
 *
 * ABI:
 *   Internal-only. This header is not part of the frozen `docs/abi/`
 *   surface; the module exposes its port handles to in-kernel callers
 *   (launcher, registry walkers) only. The on-wire envelope continues
 *   to be the canonical `ipc_msg_v0` defined by `kernel/ipc/ipc_msg.h`.
 *
 * Interactions:
 *   - kernel/ipc/ipc_port.{c,h}: port table; this module is one client.
 *   - kernel/cap/capability.h: `CAP_FS_READ`/`CAP_FS_WRITE` send/recv gates.
 *   - tests/harness/svc_subjects.h: canonical subject ids.
 *
 * Launched by:
 *   `fs_svc_init()` is called by `kernel/core/kmain.c` at boot after
 *   `ipc_port_table_init()` and `console_svc_init()` and before any
 *   module-registry walk that may try to mint a handle for the fs
 *   ports (boot-order edge documented in the implementation). In host
 *   tests, the test driver calls it directly after
 *   `ipc_port_table_reset()`.
 *
 * Issue: #278. Plan: plans/2026-05-24-m3-fs-on-m1-substrate.md slice 1.
 */

#ifndef SECUREOS_KERNEL_SVC_FS_SVC_H
#define SECUREOS_KERNEL_SVC_FS_SVC_H

#include <stdbool.h>

#include "../cap/capability.h"
#include "../ipc/ipc_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Result vocabulary for the slice-1 init entry point. Distinct from
 * `ipc_result_t` so callers can tell module-level wiring failures
 * (already initialised, port table full on either port) apart from
 * IPC-level errors. Mirrors `console_svc_result_t` from #272.
 */
typedef enum {
  FS_SVC_OK = 0,
  FS_SVC_ERR_PORT_ALLOC_READ = 1,
  FS_SVC_ERR_PORT_ALLOC_WRITE = 2,
  FS_SVC_ERR_ALREADY_INIT = 3,
} fs_svc_result_t;

/*
 * Allocate the two well-known fs-service ports. Idempotent-by-error:
 * a second call before `fs_svc_reset()` returns
 * `FS_SVC_ERR_ALREADY_INIT` and does not allocate fresh ports.
 *
 * On success the read port:
 *   - is owned by `SUBJECT_M3_FS_SVC`,
 *   - has `send_cap = CAP_FS_READ`,
 *   - has `recv_cap = CAP_FS_READ`.
 * On success the write port:
 *   - is owned by `SUBJECT_M3_FS_SVC`,
 *   - has `send_cap = CAP_FS_WRITE`,
 *   - has `recv_cap = CAP_FS_WRITE`.
 *
 * Failure path: if the read-port allocation fails, no write port is
 * allocated and `FS_SVC_ERR_PORT_ALLOC_READ` is returned. If the read
 * port succeeds but the write port fails, the read port is left
 * allocated (the caller is responsible for `ipc_port_table_reset()`
 * to clean up; the fs_svc module state stays uninitialised so a retry
 * after reset is well-defined).
 *
 * The port handles are retrievable via `fs_svc_port_read()` /
 * `fs_svc_port_write()` until reset.
 */
fs_svc_result_t fs_svc_init(void);

/*
 * Tear down the in-memory state. Does NOT call `ipc_port_destroy()` on
 * the underlying handles — callers that want the ports released must
 * either `ipc_port_table_reset()` (test harness) or land slice 4's
 * destroy wiring. Always safe to call; no-op when not initialised.
 *
 * Provided so test setup/teardown can run in any order without leaking
 * state across phases. Same contract as `console_svc_reset()`.
 */
void fs_svc_reset(void);

/*
 * Return the read-port handle allocated by `fs_svc_init()`. Returns
 * `IPC_PORT_INVALID` when the service has not been initialised. The
 * launcher (slice 2, #279) calls this when minting handles for a
 * spawned app.
 */
ipc_port_t fs_svc_port_read(void);

/*
 * Return the write-port handle allocated by `fs_svc_init()`. Returns
 * `IPC_PORT_INVALID` when the service has not been initialised.
 */
ipc_port_t fs_svc_port_write(void);

/*
 * Return true iff `fs_svc_init()` has been called and not yet reset.
 * Exposed so the validator test can assert exact init/teardown lifecycle.
 */
bool fs_svc_is_initialised(void);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_SVC_FS_SVC_H */
