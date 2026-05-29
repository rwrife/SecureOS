/**
 * @file include/clib/malloc.h
 * @brief Freestanding userland heap allocator (M7-TOOLCHAIN-001, issue #404).
 *
 * Purpose:
 *   Slice 1 of the in-OS toolchain (plan
 *   `plans/2026-05-28-in-os-toolchain-self-hosting.md` P1). TinyCC and any
 *   future SDK app linking `user/libs/clib` need a real heap, not a static
 *   buffer. This header declares the freestanding allocator surface;
 *   `src/malloc.c` implements a deterministic boundary-tag free-list on top
 *   of an arena window that the embedder grows via a `clib_brk_fn`
 *   callback.
 *
 *   The callback indirection lets the allocator land **standalone** — host
 *   unit tests drive it with a sbrk-on-a-`malloc(3)`-block shim today
 *   (`tests/clib_malloc_test.c`), and the userland runtime will wire it to
 *   the `os_mem_brk` syscall once that lands (M7-TOOLCHAIN-001 kernel-side
 *   follow-up, ABI minor bump). The issue body explicitly allows this
 *   "land standalone and fold in later" path.
 *
 * Containment:
 *   Freestanding. No libc, no kernel includes, no syscalls. The only
 *   external symbol the allocator calls is the `clib_brk_fn` the embedder
 *   registered with `clib_malloc_init`.
 *
 * Interactions:
 *   - `src/malloc.c`         — implementation (boundary-tag, first-fit).
 *   - `tests/clib_malloc_test.c` — host unit test, drives the allocator
 *     against a host-malloc-backed brk shim and covers the
 *     `toolchain_heap_isolation` second-run case from the M7 plan.
 *   - `build/scripts/test_clib_malloc.sh` — `TEST:PASS:clib_malloc` driver.
 *
 * Issue: #404. Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md (P1).
 */

#ifndef SECUREOS_USER_LIBS_CLIB_MALLOC_H
#define SECUREOS_USER_LIBS_CLIB_MALLOC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Arena-extension callback. The embedder owns the arena window — it is
 * the only entity that knows how to grow it (host-side: bump a pointer
 * into a host-malloc-backed buffer; on-target: `os_mem_brk(delta)`).
 *
 * Contract:
 *   - `delta` is always non-negative (the allocator never asks the
 *     embedder to shrink in v0 — `free` returns blocks to the free
 *     list, it does not release pages).
 *   - On success, returns a pointer to the start of the freshly
 *     committed `delta` bytes. The new region MUST be contiguous with
 *     the previously returned region (so the allocator can fold the
 *     tail-free block into the new tail).
 *   - On failure (out of arena), returns NULL. The allocator then
 *     fails the originating `malloc`/`realloc` call cleanly (returns
 *     NULL) without panicking — matches the issue's acceptance
 *     "out-of-arena growth fails cleanly (no panic)".
 *
 * `ctx` is opaque to the allocator; passed through verbatim from
 * `clib_malloc_init`.
 */
typedef void *(*clib_brk_fn)(void *ctx, size_t delta);

/*
 * Initialise the allocator with a fixed seed window
 * `[seed_base, seed_base + seed_size)` and a `brk` callback used to
 * extend the window on demand. Both `seed_base` and `seed_size` may be
 * zero, in which case the very first `malloc` triggers a `brk` call.
 *
 * Idempotent within a process: calling `clib_malloc_init` a second
 * time wipes the allocator state — this is the
 * `toolchain_heap_isolation` second-run case from the M7 plan, where
 * the same in-process compiler instance is re-entered for a second
 * translation unit and must not leak state across runs.
 *
 * Returns 0 on success, -1 on invalid argument (seed_size > 0 with
 * NULL seed_base, or `seed_size` smaller than the minimum bookkeeping
 * footprint reported by `clib_malloc_min_seed_bytes`).
 */
int clib_malloc_init(void *seed_base,
                     size_t seed_size,
                     clib_brk_fn brk_fn,
                     void *brk_ctx);

/*
 * Tear down: forget seed + brk. Safe to call without a prior init.
 * Provided so the host test can deterministically reset state between
 * the two halves of the isolation check.
 */
void clib_malloc_shutdown(void);

/*
 * Minimum bytes the embedder must hand to `clib_malloc_init` for the
 * seed window to be usable for at least one allocation. Returned as a
 * function (not a macro) so the value travels with the binary and the
 * tests pin it from the same source of truth.
 */
size_t clib_malloc_min_seed_bytes(void);

/*
 * Standard allocator surface. Names match the C standard so a future
 * TinyCC build (M7-TOOLCHAIN-005) can link against `libclib.a`
 * unchanged. All allocations are 16-byte aligned, which is sufficient
 * for any scalar type and for the SysV x86_64 stack-alignment rule
 * the compiler emits.
 */
void *clib_malloc(size_t size);
void  clib_free(void *ptr);
void *clib_realloc(void *ptr, size_t size);
void *clib_calloc(size_t nmemb, size_t size);

/*
 * Snapshot introspection — used by the host test to assert leak-free
 * teardown and to drive the second-run isolation check. NOT part of
 * the on-target ABI; the implementation is allowed to keep these
 * out of the on-image build with `-DCLIB_MALLOC_NO_INTROSPECT` if
 * code-size becomes a concern. The host test always compiles them in.
 */
typedef struct clib_malloc_stats {
  size_t bytes_in_use;     /* sum of payload bytes currently handed out */
  size_t bytes_total;      /* total arena bytes the allocator owns */
  size_t free_block_count; /* number of blocks on the free list */
  size_t live_alloc_count; /* number of outstanding allocations */
} clib_malloc_stats_t;

void clib_malloc_get_stats(clib_malloc_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_MALLOC_H */
