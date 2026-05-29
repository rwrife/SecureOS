/*
 * user/libs/clib/src/qsort.c
 *
 * Freestanding `qsort` for the in-OS toolchain libc nucleus
 * (issue #407 / M7-TOOLCHAIN-004, plan
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3).
 *
 * Implementation notes:
 *   - Iterative median-of-three quicksort over a Lomuto partition,
 *     with an explicit fixed-size stack and an insertion-sort
 *     fallback for short runs. Iterative form bounds stack use
 *     independently of input size; the explicit stack is sized for
 *     `2 * log2(SIZE_MAX)` which dominates anything we can possibly
 *     be asked to sort on a 64-bit host or a 32-bit in-OS userland.
 *   - Always recurses on the smaller partition first; with median-of-
 *     three pivoting this caps stack depth at O(log n) on
 *     pathological inputs (sorted, reverse-sorted).
 *   - Element swap is done byte-wise so the routine has no alignment
 *     assumption on `size`. TinyCC sorts heterogeneous element sizes;
 *     this keeps us correct for unaligned element widths.
 *   - No allocator dependency — qsort is in-place.
 *   - No libc dependency, no syscall dependency. Matches the rest of
 *     `user/libs/clib`: freestanding, host-testable, no
 *     `OS_ABI_VERSION` impact.
 *
 * Edge cases handled per the canonical C89 contract (see header):
 *   - `nmemb < 2` → no-op.
 *   - `size == 0` → no-op (treat as nothing to sort).
 *   - `base == NULL` with `nmemb == 0` → no-op (caller-valid).
 *   - `compar == NULL` → no-op (defensive; the canonical contract
 *     leaves this UB, but a NULL deref would just crash the in-OS
 *     toolchain instead of returning a useful error).
 */

#include <stddef.h>
#include <stdint.h>

#include "../include/clib/qsort.h"

/* swap_bytes: byte-wise swap of two non-overlapping element-sized
 * regions. Keeps us alignment-agnostic; the optimiser collapses the
 * word-sized cases into wider moves on common targets. */
static void swap_bytes(unsigned char *a, unsigned char *b, size_t n) {
  if (a == b) return;
  while (n--) {
    unsigned char t = *a;
    *a++ = *b;
    *b++ = t;
  }
}

/* Insertion-sort for short ranges. Quicksort hands off to this when
 * the partition shrinks below `INSERT_SORT_THRESHOLD`; insertion-sort
 * dominates on tiny inputs and removes the worst-case behaviour of
 * recursing on size-2/3 partitions. */
#define INSERT_SORT_THRESHOLD ((size_t)8)

static void insertion_sort(unsigned char *base,
                           size_t nmemb,
                           size_t size,
                           int (*compar)(const void *, const void *)) {
  size_t i;
  for (i = 1; i < nmemb; i++) {
    size_t j = i;
    while (j > 0) {
      unsigned char *cur = base + j * size;
      unsigned char *prev = cur - size;
      if (compar(prev, cur) <= 0) break;
      swap_bytes(prev, cur, size);
      j--;
    }
  }
}

/* Median-of-three: choose median of {first, middle, last}, leave it
 * at the LAST position so the Lomuto partition below can treat the
 * high element as the pivot uniformly. nmemb is known >= 3 at call
 * sites. */
static void median_of_three_to_high(unsigned char *base,
                                    size_t nmemb,
                                    size_t size,
                                    int (*compar)(const void *,
                                                  const void *)) {
  unsigned char *lo  = base;
  unsigned char *mid = base + (nmemb / 2) * size;
  unsigned char *hi  = base + (nmemb - 1) * size;

  /* Order lo / mid / hi so *lo <= *mid <= *hi. */
  if (compar(lo,  mid) > 0) swap_bytes(lo,  mid, size);
  if (compar(lo,  hi)  > 0) swap_bytes(lo,  hi,  size);
  if (compar(mid, hi)  > 0) swap_bytes(mid, hi,  size);

  /* Move the median (currently at `mid`) to `hi`, so Lomuto can use
   * the last element as the pivot. The old `hi` (>= median) lands at
   * `mid`; the invariant `*lo <= pivot` still holds, which the
   * partition loop relies on as a left sentinel. */
  swap_bytes(mid, hi, size);
}

/*
 * Lomuto partition with the pivot at the high end. Scans
 * [base .. base+(n-2)*size], moves every element < pivot to the left.
 * Returns the final pivot index `p` such that the array is
 * partitioned as:
 *
 *     base[0..p-1] <= pivot
 *     base[p]     == pivot
 *     base[p+1..n-1] >= pivot
 */
static size_t lomuto_partition(unsigned char *base,
                               size_t nmemb,
                               size_t size,
                               int (*compar)(const void *, const void *)) {
  unsigned char *pivot = base + (nmemb - 1) * size;
  size_t store = 0;
  size_t i;
  for (i = 0; i + 1 < nmemb; i++) {
    unsigned char *cur = base + i * size;
    if (compar(cur, pivot) < 0) {
      swap_bytes(cur, base + store * size, size);
      store++;
    }
  }
  swap_bytes(base + store * size, pivot, size);
  return store;
}

/* Stack frame for the iterative quicksort. */
typedef struct qs_frame {
  unsigned char *base;
  size_t         nmemb;
} qs_frame;

/* 2 * log2(SIZE_MAX) + a few slots of slop. Generous; we never push
 * more than O(log n) frames because we always recurse on the smaller
 * side and tail-loop on the larger. */
#define QSORT_STACK_DEPTH 96

void qsort(void *base_v,
           size_t nmemb,
           size_t size,
           int (*compar)(const void *, const void *)) {
  if (nmemb < 2 || size == 0 || compar == 0) return;
  if (base_v == 0) return; /* defensive: legal `NULL + nmemb=0` was
                            * already shorted out above. */

  qs_frame stack[QSORT_STACK_DEPTH];
  int sp = 0;
  stack[sp].base  = (unsigned char *)base_v;
  stack[sp].nmemb = nmemb;
  sp++;

  while (sp > 0) {
    sp--;
    unsigned char *base = stack[sp].base;
    size_t         n    = stack[sp].nmemb;

    /* Tail-loop on the current range so we don't push trivial frames. */
    for (;;) {
      if (n < INSERT_SORT_THRESHOLD) {
        insertion_sort(base, n, size, compar);
        break;
      }

      median_of_three_to_high(base, n, size, compar);
      size_t p = lomuto_partition(base, n, size, compar);

      /* Left = [base .. p-1] (length p),
       * Right = [base + (p+1)*size .. base + (n-1)*size] (length n-p-1). */
      size_t left_n  = p;
      size_t right_n = n - p - 1;

      /* Recurse on smaller side, tail-loop on larger. */
      if (left_n < right_n) {
        if (left_n > 1) {
          if (sp >= QSORT_STACK_DEPTH) {
            /* Defensive: unreachable for any realistic `size`. Fall
             * back to insertion-sort on the remaining range so we
             * still terminate. */
            insertion_sort(base, n, size, compar);
            break;
          }
          stack[sp].base  = base;
          stack[sp].nmemb = left_n;
          sp++;
        }
        if (right_n > 1) {
          base = base + (p + 1) * size;
          n    = right_n;
          continue;
        }
        break;
      } else {
        if (right_n > 1) {
          if (sp >= QSORT_STACK_DEPTH) {
            insertion_sort(base, n, size, compar);
            break;
          }
          stack[sp].base  = base + (p + 1) * size;
          stack[sp].nmemb = right_n;
          sp++;
        }
        if (left_n > 1) {
          n = left_n;
          continue;
        }
        break;
      }
    }
  }
}
