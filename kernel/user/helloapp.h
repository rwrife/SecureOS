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

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_USER_HELLOAPP_H */
