#ifndef CLIB_QSORT_H
#define CLIB_QSORT_H

/*
 * user/libs/clib/include/clib/qsort.h
 *
 * Freestanding `qsort` for the in-OS toolchain libc nucleus
 * (issue #407 / M7-TOOLCHAIN-004, plan
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3).
 *
 * TinyCC's symbol generator + a number of compiler internals call
 * `qsort` directly; the in-tree sort therefore needs to ship with the
 * libc nucleus so the in-OS toolchain links against `libclib.a` rather
 * than pulling in a hosted libc. No syscall dependency.
 *
 * Contract (matches the canonical C89/C99 surface):
 *
 *     void qsort(void *base,
 *                size_t nmemb,
 *                size_t size,
 *                int (*compar)(const void *, const void *));
 *
 *   - Sorts `nmemb` elements of `size` bytes each in-place under the
 *     ordering induced by `compar`. `compar(a, b)` returns negative,
 *     zero, or positive iff `a < b`, `a == b`, or `a > b`.
 *   - Stability is **not** guaranteed (matches C89; the standard does
 *     not require qsort to be stable).
 *   - `nmemb == 0` and `nmemb == 1` are no-ops.
 *   - `size == 0` is treated as a no-op (no UB; callers asking us to
 *     sort zero-byte elements clearly mean nothing).
 *   - `base == NULL` is only legal when `nmemb == 0` (matches glibc /
 *     musl behaviour and avoids a NULL deref under valid inputs).
 *
 * Used by:
 *   - TinyCC tokenizer / symbol sort paths (#403 P4 driver).
 *   - SDK consumers that want a sort without a hosted libc.
 *
 * No allocator dependency (qsort is in-place). No locale, no thread
 * state. Same ABI status as the rest of `user/libs/clib`: userland-
 * only, additive, no `OS_ABI_VERSION` bump.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical libc name so TinyCC and SDK consumers link "for free" —
 * matches the str / mem / ctype slice convention (PRs 416 / 417).
 */
void qsort(void *base,
           size_t nmemb,
           size_t size,
           int (*compar)(const void *, const void *));

#ifdef __cplusplus
}
#endif

#endif /* CLIB_QSORT_H */
