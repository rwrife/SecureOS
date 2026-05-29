/*
 * user/libs/clib/src/bsearch.c
 *
 * Freestanding `bsearch` for the in-OS toolchain libc nucleus
 * (issue #407 / M7-TOOLCHAIN-004 slice 7, plan
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3).
 *
 * Implementation notes:
 *   - Iterative binary search over `[lo, hi)` half-open range with
 *     the canonical `mid = lo + (hi - lo) / 2` form so the midpoint
 *     computation cannot overflow even for `nmemb` close to
 *     `SIZE_MAX`.
 *   - Pointer arithmetic is done in `unsigned char` units so the
 *     routine has no alignment assumption on `size` — same posture
 *     as the qsort slice's byte-wise swap. TinyCC's symbol table
 *     uses heterogeneous element sizes, so the alignment-agnostic
 *     form is the right baseline.
 *   - Defensive on every UB input the canonical contract leaves
 *     unspecified (NULL key, NULL base with nmemb>0, NULL compar,
 *     size==0): return NULL instead of dereferencing. Matches the
 *     qsort slice's "no-op on UB inputs" stance and keeps the in-OS
 *     toolchain from crashing on a programmer mistake.
 *   - No libc dependency, no syscall dependency. Matches the rest
 *     of `user/libs/clib`: freestanding, host-testable, no
 *     `OS_ABI_VERSION` impact.
 */

#include <stddef.h>

#include "../include/clib/bsearch.h"

void *bsearch(const void *key,
              const void *base,
              size_t nmemb,
              size_t size,
              int (*compar)(const void *, const void *)) {
  /* Defensive: every UB-on-paper input degrades to "miss" rather
   * than NULL deref. Mirrors the qsort slice's NULL/empty handling
   * so the libc nucleus has a consistent failure mode. */
  if (key == NULL || compar == NULL) {
    return NULL;
  }
  if (nmemb == 0 || size == 0) {
    return NULL;
  }
  if (base == NULL) {
    /* nmemb > 0 from the check above → base==NULL is caller-UB. */
    return NULL;
  }

  const unsigned char *bytes = (const unsigned char *)base;
  size_t lo = 0;
  size_t hi = nmemb; /* half-open: searched range is [lo, hi). */

  while (lo < hi) {
    /* Overflow-safe midpoint. */
    size_t mid = lo + (hi - lo) / 2;
    const void *elem = (const void *)(bytes + mid * size);
    int r = compar(key, elem);
    if (r < 0) {
      hi = mid;
    } else if (r > 0) {
      lo = mid + 1;
    } else {
      /* Hit. C standard does not pin which equal element we return
       * when there are duplicates; the natural midpoint is fine. */
      return (void *)elem; /* cast away const per C89 surface. */
    }
  }
  return NULL;
}
