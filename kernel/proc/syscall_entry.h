/**
 * @file syscall_entry.h
 * @brief M1 syscall entry stub — reserves the ABI vector range without
 *        wiring any actual call sites (issue #232, plan #198).
 *
 * Purpose:
 *   `plans/2026-05-20-m1-process-address-space.md` §"Kernel/user
 *   separation strategy in M1" calls for a *reserved-but-unused*
 *   syscall entry vector so the ABI slot is locked under
 *   `OS_ABI_VERSION = 0` (#150) before any caller is added. This file
 *   is the single source of truth for that vector range; once any
 *   real M2+ syscall is wired, its numeric vector MUST be allocated
 *   from `[SYSCALL_VECTOR_BASE, SYSCALL_VECTOR_BASE + SYSCALL_VECTOR_COUNT)`
 *   and remain stable across ABI majors.
 *
 *   In M1, `kernel_syscall_entry` returns `IPC_ERR_INVALID_MSG` for
 *   every vector (matching the IPC error vocabulary so the kernel does
 *   not yet need a second status enum). Any invocation also emits the
 *   canonical `CAP:DENY:<actor>:syscall:-` marker via the shared
 *   `cap_deny_marker` formatter so the deny-marker contract tests
 *   (#211 / #221 shape) apply uniformly once a caller is wired.
 *
 * Interactions:
 *   - kernel/cap/capability.h: declares `CAP_SYSCALL = 15`, the
 *     capability gating future M2+ callers.
 *   - kernel/cap/cap_deny_marker.{c,h}: canonical marker formatter
 *     used by `kernel_syscall_entry` on its deny path.
 *   - kernel/ipc/ipc_msg.h: `ipc_result_t` / `IPC_ERR_INVALID_MSG`.
 *   - user/include/secureos_abi.h: anchors `OS_ABI_VERSION = 0` so the
 *     vector range freeze is observable on both sides of the boundary.
 *   - docs/abi/syscalls.md: human-readable record of the reserved
 *     range and the "returns INVALID_MSG in M1" contract.
 *
 * Launched by:
 *   Header-only declarations; not a standalone process. The future
 *   ring-3 transition (M2+) is the only caller of
 *   `kernel_syscall_entry` outside the unit-test harness.
 */

#ifndef SECUREOS_KERNEL_PROC_SYSCALL_ENTRY_H
#define SECUREOS_KERNEL_PROC_SYSCALL_ENTRY_H

#include <stddef.h>
#include <stdint.h>

#include "../cap/capability.h"
#include "../ipc/ipc_msg.h"
#include "../../user/include/secureos_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reserved syscall vector range — frozen under OS_ABI_VERSION = 0.
 *
 * The range is intentionally small at M1 (16 slots). It can be widened
 * additively without breaking the freeze; numeric values inside the
 * range MUST NOT be renumbered or reused for a different purpose once
 * any real caller binds to one.
 *
 * Vectors outside [SYSCALL_VECTOR_BASE, SYSCALL_VECTOR_BASE + SYSCALL_VECTOR_COUNT)
 * are not part of the SecureOS syscall ABI and `kernel_syscall_entry`
 * rejects them with `IPC_ERR_INVALID_MSG` just like in-range vectors
 * do in M1 — the rejection reason is "out of range" rather than
 * "stubbed". Callers cannot distinguish today; the distinction will
 * matter in M2+ when in-range vectors start returning real results.
 */
#define SYSCALL_VECTOR_BASE   ((uint32_t)0x0000u)
#define SYSCALL_VECTOR_COUNT  ((uint32_t)16u)
#define SYSCALL_VECTOR_LIMIT  (SYSCALL_VECTOR_BASE + SYSCALL_VECTOR_COUNT)

/*
 * Packed (OS_ABI_VERSION << 16) | SYSCALL_VECTOR_COUNT anchor. The unit
 * test cross-checks this value against `OS_ABI_VERSION` from
 * `user/include/secureos_abi.h` so the syscall reservation cannot
 * silently drift away from the ABI freeze.
 */
#define SYSCALL_ENTRY_ABI_ANCHOR \
    (((uint32_t)OS_ABI_VERSION << 16) | SYSCALL_VECTOR_COUNT)

/*
 * Stub syscall entry. In M1 this function returns `IPC_ERR_INVALID_MSG`
 * for every vector and emits a canonical `CAP:DENY` marker for
 * `CAP_SYSCALL`. The argument registers are accepted but ignored.
 *
 * `caller_subject` is the subject identity the future ring-3 transition
 * will stamp; the unit-test harness passes a synthetic subject id so
 * the deny marker is observable.
 *
 * Pure host-side compilable: no <stdio.h> in this header.
 */
ipc_result_t kernel_syscall_entry(cap_subject_id_t caller_subject,
                                  uint32_t vector,
                                  uintptr_t arg0,
                                  uintptr_t arg1,
                                  uintptr_t arg2);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_PROC_SYSCALL_ENTRY_H */
