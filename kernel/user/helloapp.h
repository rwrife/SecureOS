/**
 * @file helloapp.h
 * @brief HelloApp M2-on-M1 substrate module (slice 3 of plan #263).
 *
 * Purpose:
 *   Single-shot in-kernel user module that exercises the M1→M2 initial
 *   capability handoff documented in `docs/architecture/m1-m2-handoff.md`:
 *
 *     1. Reads its handed-off `cap_handle_t` little-endian from the
 *        first four bytes of `address_space_t::ipc_scratch` (the slot
 *        the launcher's slice-2 spawn writes per #269).
 *     2. Builds one canonical `ipc_msg_v0` envelope whose payload is
 *        the ASCII string `"helloapp\n"` (length 9).
 *     3. Calls `ipc_send_h(handle, console_port, &msg)` exactly once
 *        and returns the resulting `ipc_result_t` verbatim.
 *
 *   The module body is intentionally a single function instead of a
 *   scheduler-driven entry stub so the `_qemu` host validators
 *   (#270) can call it inline after `launcher_spawn_app_from_manifest()`
 *   without spinning up `proc_sched_run_until_idle()`. The slice's
 *   acceptance signal is the IPC send's success/deny — not the
 *   scheduler shape — so this keeps the test surface narrow.
 *
 *   Slice scope (issue #270):
 *     - One module, two `_qemu`-tier tests (allow + deny).
 *     - NO forwarding from the console-svc port to the kernel console
 *       driver — slice 4 (#271) owns the launcher_console peer that
 *       drains the port.
 *     - NO ELF / FS-backed loading — that is M3.
 *
 * ABI:
 *   Internal-only. Not part of the frozen `docs/abi/` surface.
 *
 * Interactions:
 *   - kernel/proc/address_space.h: reads `ipc_scratch` per the handoff
 *     convention; never writes back.
 *   - kernel/ipc/ipc_ops.h:        `ipc_send_h` is the only IPC call.
 *   - kernel/ipc/ipc_msg.h:        envelope shape, `ipc_result_t`.
 *   - kernel/cap/cap_handle.h:     handle type passed through.
 *
 * Launched by:
 *   `tests/m2_helloapp_allow_qemu_test.c` and
 *   `tests/m2_helloapp_deny_qemu_test.c` after spawning the app via
 *   `launcher_spawn_app_from_manifest()`. The kernel boot sequence
 *   does not yet auto-launch this module — that wiring lands with
 *   the slice-4 launcher_console peer.
 *
 * Issue: #270. Plan: plans/2026-05-23-m2-on-m1-substrate.md slice 3.
 */

#ifndef SECUREOS_KERNEL_USER_HELLOAPP_H
#define SECUREOS_KERNEL_USER_HELLOAPP_H

#include "../ipc/ipc_msg.h"
#include "../ipc/ipc_port.h"
#include "../proc/address_space.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical HelloApp banner. The M2 substrate spec (BUILD_ROADMAP
 * §5.2) calls for `"helloapp\n"`; both `_qemu` tests pin this exact
 * byte sequence so any unintended widening of the payload is caught
 * by the receive-side assertion. Length is 9 (8 chars + newline).
 */
#define HELLOAPP_BANNER         "helloapp\n"
#define HELLOAPP_BANNER_LEN     ((size_t)9u)

/*
 * Run the HelloApp module body exactly once.
 *
 * Parameters:
 *   - aspace       : the spawned app's address space window. MUST be
 *                    non-NULL with a non-NULL `ipc_scratch`. The first
 *                    four bytes are interpreted little-endian as the
 *                    `cap_handle_t` the launcher handed off.
 *   - console_port : well-known port handle for the console service
 *                    (typically `console_svc_port()`).
 *
 * Returns:
 *   - The unmodified `ipc_result_t` from `ipc_send_h(handle, port, msg)`.
 *     IPC_OK on the allow path; IPC_ERR_CAP_DENIED on the deny path
 *     (with the canonical CAP:DENY marker already emitted by the IPC
 *     layer). IPC_ERR_INVALID_PORT on a bogus port; IPC_ERR_INVALID_MSG
 *     if `aspace` or its `ipc_scratch` is NULL.
 *
 * Side effects:
 *   - On the allow path, stages one envelope into `console_port`'s
 *     single-waiter slot. The caller (test driver or, later, the
 *     slice-4 launcher_console peer) drains it via `ipc_port_consume()`.
 *   - On the deny path, the IPC layer emits exactly one canonical
 *     `CAP:DENY:<subject>:console_write:-` marker line to stdout via
 *     `cap_deny_marker_format()` (the shape gated by #221 / PR #244).
 */
ipc_result_t helloapp_run_once(const address_space_t *aspace,
                               ipc_port_t console_port);

/* ---------------- M3 fs-demo entry (slice 3 of plan #277, issue #280)
 *
 * Opt-in second entry point that exercises the M3-on-M1 fs round-trip
 * the same way `helloapp_run_once` exercised the M2 console round-trip.
 *
 * Layout consumed (matches `launcher_fs_spawn_app_with_fs_caps`, #279):
 *   ipc_scratch[ 0.. 7) : reserved (M1 single-handle handoff)
 *   ipc_scratch[ 8..16) : LE64(cap_handle_t) — CAP_FS_READ
 *   ipc_scratch[16..24) : LE64(cap_handle_t) — CAP_FS_WRITE
 *                         (CAP_HANDLE_NULL when not granted)
 *
 * Behaviour:
 *   1. Decode the read + write fs handles from ipc_scratch.
 *   2. Build a canonical `ipc_msg_v0` write request (payload
 *      `HELLOAPP_FS_DEMO_BLOB`, length `HELLOAPP_FS_DEMO_BLOB_LEN`)
 *      and call `ipc_send_h(write_handle, fs_write_port, &write_req)`.
 *      Record the result in `out->write_send_result`.
 *   3. Build a canonical `ipc_msg_v0` read request (payload
 *      `HELLOAPP_FS_DEMO_PATH`, length `HELLOAPP_FS_DEMO_PATH_LEN`)
 *      and call `ipc_send_h(read_handle, fs_read_port, &read_req)`.
 *      Record the result in `out->read_send_result`.
 *   4. For each leg that returned `IPC_OK`, emit exactly one line of
 *      `TEST:PASS:m3_helloapp_fs_qemu_op\n` to stdout (one per op,
 *      per issue #280's "On IPC_OK for each: emit ... (one per op)").
 *
 * Notes:
 *   - The demo does NOT spin a recv loop on the fs ports; that
 *     drain happens on the test driver side via `ipc_recv_h`, the
 *     same pattern `helloapp_run_once`/console-svc uses today.
 *   - The deny path of issue #280 is observable by the test through
 *     `out->write_send_result == IPC_ERR_CAP_DENIED` plus the
 *     canonical `CAP:DENY` marker emitted by `ipc_send_h`. This
 *     function never short-circuits the read leg, so the test sees
 *     both result codes for either allow or deny configurations.
 *   - Both ports being `IPC_PORT_INVALID` short-circuits to
 *     `IPC_ERR_INVALID_PORT` for the affected leg.
 *   - `aspace == NULL` or a NULL `ipc_scratch` returns
 *     `{ IPC_ERR_INVALID_MSG, IPC_ERR_INVALID_MSG }`.
 *   - `out` MUST be non-NULL.
 *
 * Issue: #280. Plan: plans/2026-05-24-m3-fs-on-m1-substrate.md slice 3.
 */

#define HELLOAPP_FS_DEMO_PATH      "note.txt"
#define HELLOAPP_FS_DEMO_PATH_LEN  ((size_t)8u)
#define HELLOAPP_FS_DEMO_BLOB      "persisted-by-helloapp"
#define HELLOAPP_FS_DEMO_BLOB_LEN  ((size_t)21u)

typedef struct {
  ipc_result_t write_send_result;
  ipc_result_t read_send_result;
} helloapp_fs_demo_result_t;

void helloapp_entry_fs_demo(const address_space_t *aspace,
                            ipc_port_t fs_read_port,
                            ipc_port_t fs_write_port,
                            helloapp_fs_demo_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_USER_HELLOAPP_H */
