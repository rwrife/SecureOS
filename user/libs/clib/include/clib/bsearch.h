#ifndef CLIB_BSEARCH_H
#define CLIB_BSEARCH_H

/*
 * user/libs/clib/include/clib/bsearch.h
 *
 * Freestanding `bsearch` for the in-OS toolchain libc nucleus
 * (issue #407 / M7-TOOLCHAIN-004, plan
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3).
 *
 * Slice 7 of the freestanding libc nucleus, peer to:
 *   - slice 1 (string.h: str / mem family, PR #416, merged)
 *   - slice 2 (ctype.h:  is / to family,  PR #417, merged)
 *   - slice 3 (qsort.h:  qsort,           PR #418)
 *   - slice 4 (stdlib.h: atoi/strtol/strtoul/abs/labs, PR #428)
 *   - slice 5 (errno.h:  errno + macros + strerror, PR #430)
 *   - slice 6 (stdarg.h: va_ family, PR #431)
 *
 * Sibling — not stacked. `bsearch` has no syscall dependency, no
 * allocator dependency, and (critically) no link-time dependency on
 * the other clib TUs. It ships in its own header / source / test /
 * `symbol_set_pinned` sub-marker, so it can land alongside the open
 * #407 slices (#418 / #428 / #430 / #431) in any order with only
 * trivial textual merges in `test.sh` / `validate_bundle.sh` /
 * `README.md`.
 *
 * Why now / why not folded into the qsort slice (PR #418):
 *   TinyCC's tokenizer + symbol-table lookup paths sort-then-search
 *   into the same arrays they fed to qsort; the C standard pairs
 *   the two functions in `<stdlib.h>` precisely because callers
 *   typically need both. Shipping bsearch as an explicit peer of
 *   qsort closes that gap before the TinyCC port (#408) starts
 *   linking against the libc nucleus. PR #418's scope was
 *   intentionally "qsort only" so it could be reviewed in isolation;
 *   this is the matching companion slice.
 *
 * Contract (matches the canonical C89/C99 surface):
 *
 *     void *bsearch(const void *key,
 *                   const void *base,
 *                   size_t nmemb,
 *                   size_t size,
 *                   int (*compar)(const void *, const void *));
 *
 *   - Searches a SORTED array of `nmemb` elements of `size` bytes
 *     each for a match under the ordering induced by `compar`.
 *   - `compar(key, elem)` returns negative / zero / positive iff
 *     `key < elem`, `key == elem`, `key > elem`. Same convention as
 *     `qsort`.
 *   - Returns a pointer to a matching element on hit, or `NULL` on
 *     miss. If multiple elements compare equal to the key, which one
 *     is returned is unspecified (matches the C standard).
 *   - `nmemb == 0` is defined: returns NULL.
 *   - `size == 0` is treated as no-match → NULL (no UB; callers
 *     asking us to search zero-byte elements clearly mean nothing,
 *     mirrors the qsort slice's `size == 0` no-op convention).
 *   - `base == NULL` is legal only when `nmemb == 0` (mirrors the
 *     qsort slice's NULL/empty contract); otherwise the caller is
 *     in UB territory and we return NULL defensively.
 *   - `key == NULL` / `compar == NULL` → NULL (defensive; the
 *     canonical contract leaves both UB, but returning NULL keeps
 *     the in-OS toolchain from crashing on a programmer mistake).
 *
 * Used by:
 *   - TinyCC symbol-table lookup paths (#408 freestanding port).
 *   - SDK consumers that want a binary search without a hosted libc.
 *
 * No allocator dependency. No locale, no thread state. Same ABI
 * status as the rest of `user/libs/clib`: userland-only, additive,
 * no `OS_ABI_VERSION` bump (parity with slices 1/2/3/4/5/6 and
 * `clib_malloc`).
 *
 * Interactions:
 *   - src/bsearch.c              — implementation (iterative,
 *                                  branch-thin, alignment-agnostic).
 *   - tests/clib_bsearch_test.c  — host unit test, compiled with
 *                                  `-fno-builtin` so the assertions
 *                                  exercise our implementation
 *                                  rather than `__builtin_bsearch`.
 *   - build/scripts/test_clib_bsearch.sh — TEST:PASS:clib_bsearch
 *                                          driver.
 *
 * Issue: 407. Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md (P3).
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical libc name so TinyCC and SDK consumers link "for free" —
 * no clib_-prefixed wrapper.
 */
void *bsearch(const void *key,
              const void *base,
              size_t nmemb,
              size_t size,
              int (*compar)(const void *, const void *));

#ifdef __cplusplus
}
#endif

#endif /* CLIB_BSEARCH_H */
