/**
 * @file include/clib/os_brk.h
 * @brief On-target `clib_brk_fn` forwarder backed by `os_mem_brk`
 *        (M7-TOOLCHAIN-001 slice 3, issue #421).
 *
 * Purpose:
 *   Slice 2 of M7-TOOLCHAIN-001 (PR #432, commit `af8ece8`) landed the
 *   `os_mem_brk(int delta, void **out_prev_break)` syscall ABI + native
 *   bridge slot. The `user/libs/clib` allocator (`include/clib/malloc.h`)
 *   takes its arena window through a `clib_brk_fn` callback registered
 *   at `clib_malloc_init`. This header declares the **single-entry
 *   forwarder** that lets an on-target SDK app wire those two halves
 *   together:
 *
 *       clib_malloc_init(NULL, 0, clib_os_brk, NULL);
 *
 *   The forwarder is a thin, signature-correct adapter — it converts
 *   the `clib_brk_fn` contract (`(ctx, delta) -> first new byte`) into
 *   the `os_mem_brk` contract (`(delta, out_prev_break) -> status`),
 *   guards the `size_t -> int` narrowing so an unreasonable `delta`
 *   fails clean instead of wrapping into a negative shrink, and maps
 *   any failure of the underlying syscall into the NULL return that
 *   the allocator already knows how to treat as "out of arena, fail
 *   the originating malloc/realloc cleanly".
 *
 *   Host tests intentionally keep their existing `brk_shim_fn` shim
 *   (`tests/clib_malloc_test.c`) — those exercise the allocator on a
 *   host-malloc-backed buffer where `os_mem_brk` is unreachable
 *   (the bridge is unmapped). The forwarder is host-buildable
 *   (it links cleanly against `user/runtime/secureos_api_stubs.c`),
 *   but on the host every call returns NULL because the runtime
 *   wrapper sees no bridge and returns `OS_STATUS_ERROR`. This
 *   "no-bridge -> NULL" branch is what `tests/clib_os_brk_test.c`
 *   pins as the host-side smoke; the live on-target growth path is
 *   covered by the deferred `clib_brk_growth_qemu` peer the issue
 *   body calls out.
 *
 * Containment:
 *   Pulls in `clib/malloc.h` for `clib_brk_fn` and `secureos_api.h`
 *   for `os_mem_brk`. No kernel headers, no libc. Same shape as
 *   `user/runtime/secureos_api_stubs.c`'s other on-target wrappers
 *   (validated against `validate_sdk_no_kernel_includes`).
 *
 * Interactions:
 *   - `include/clib/malloc.h` — defines the `clib_brk_fn` shape this
 *     forwarder satisfies.
 *   - `user/include/secureos_api.h` — declares `os_mem_brk`.
 *   - `user/runtime/secureos_api_stubs.c` — provides the `os_mem_brk`
 *     wrapper this forwarder calls.
 *   - `src/os_brk.c` — implementation.
 *   - `tests/clib_os_brk_test.c` — host link-pin + no-bridge branch
 *     coverage.
 *
 * Issue: #421. Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md
 * (P1, slice 3 follow-up to PR #432).
 */

#ifndef SECUREOS_USER_LIBS_CLIB_OS_BRK_H
#define SECUREOS_USER_LIBS_CLIB_OS_BRK_H

#include <stddef.h>

#include "malloc.h" /* clib_brk_fn */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * `clib_brk_fn`-shaped forwarder around `os_mem_brk`.
 *
 * Contract (mirrors the `clib_brk_fn` doc in `clib/malloc.h` exactly):
 *   - `ctx` is ignored (the on-target heap is process-global; the
 *     allocator's per-instance state already tracks the arena base).
 *     Accepting the parameter keeps the signature compatible so the
 *     symbol can be passed directly to `clib_malloc_init` without a
 *     wrapper trampoline.
 *   - On success returns a pointer to the first byte of the freshly
 *     committed `delta`-byte region — i.e. the `prev_break` reported
 *     by the underlying `os_mem_brk` call. The region is contiguous
 *     with the previous return (guaranteed by `os_mem_brk`'s sbrk-
 *     shape contract; see `docs/abi/syscalls.md`).
 *   - On any failure path returns NULL. Failure cases this collapses
 *     into NULL:
 *       * `delta` is zero (no arena growth requested; the allocator
 *         never calls with `delta == 0`, but we treat it as a no-op
 *         failure rather than asking the kernel for a zero-grow);
 *       * `delta` exceeds `INT_MAX` (cannot be expressed in the
 *         syscall's signed `int` parameter without wrapping into a
 *         negative shrink request);
 *       * the underlying `os_mem_brk` returns `OS_STATUS_DENIED`
 *         (out-of-arena growth — the documented clean-error case)
 *         or `OS_STATUS_ERROR` (no bridge, e.g. host build).
 *
 *   Never panics. Never partially commits. Idempotent on failure
 *   (no state mutation if the kernel rejects the grow).
 */
void *clib_os_brk(void *ctx, size_t delta);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_OS_BRK_H */
