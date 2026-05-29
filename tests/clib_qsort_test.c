/*
 * tests/clib_qsort_test.c
 *
 * Host unit test for the freestanding `qsort` shipped by
 * `user/libs/clib/src/qsort.c` (issue #407 / M7-TOOLCHAIN-004 slice
 * 3, plan `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3).
 *
 * Compiled with `-fno-builtin` (see `build/scripts/test_clib_qsort.sh`)
 * so the assertions exercise OUR `qsort`, not `__builtin_qsort` /
 * any host libc shortcut.
 *
 * Sub-markers (each must round-trip via `TEST:PASS:clib_qsort:...`):
 *   - empty_no_op
 *   - single_no_op
 *   - sorted_idempotent
 *   - reverse_sorted
 *   - random_ints
 *   - duplicates_grouped
 *   - small_under_insertion_threshold
 *   - large_pathological_no_overflow
 *   - struct_elements
 *   - byte_elements_size_one
 *   - odd_size_unaligned_elements
 *   - stable_against_model
 *   - symbol_set_pinned
 *
 * Roll-up marker:
 *   - TEST:PASS:clib_qsort     (only emitted if every sub-marker
 *                               passed and zero TEST:FAIL: lines were
 *                               recorded).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../user/libs/clib/include/clib/qsort.h"

/* ----- compare functions ----------------------------------------- */

static int cmp_int_asc(const void *a, const void *b) {
  int ia = *(const int *)a;
  int ib = *(const int *)b;
  if (ia < ib) return -1;
  if (ia > ib) return 1;
  return 0;
}

static int cmp_uchar(const void *a, const void *b) {
  unsigned char ua = *(const unsigned char *)a;
  unsigned char ub = *(const unsigned char *)b;
  if (ua < ub) return -1;
  if (ua > ub) return 1;
  return 0;
}

/* A 3-byte "odd size" element so we exercise the byte-wise swap on
 * a width that is not naturally aligned. */
typedef struct { unsigned char b[3]; } odd3_t;

static int cmp_odd3(const void *a, const void *b) {
  const odd3_t *x = (const odd3_t *)a;
  const odd3_t *y = (const odd3_t *)b;
  for (int i = 0; i < 3; i++) {
    if (x->b[i] < y->b[i]) return -1;
    if (x->b[i] > y->b[i]) return 1;
  }
  return 0;
}

/* A wide struct ordered by `key` only — the `payload` lets us check
 * that the byte-wise swap moves the whole struct, not just the key. */
typedef struct {
  int  key;
  int  payload;
} kv_t;

static int cmp_kv(const void *a, const void *b) {
  int ka = ((const kv_t *)a)->key;
  int kb = ((const kv_t *)b)->key;
  if (ka < kb) return -1;
  if (ka > kb) return 1;
  return 0;
}

/* ----- helpers --------------------------------------------------- */

static int int_sorted_ascending(const int *a, size_t n) {
  for (size_t i = 1; i < n; i++) {
    if (a[i - 1] > a[i]) return 0;
  }
  return 1;
}

static int uchar_sorted_ascending(const unsigned char *a, size_t n) {
  for (size_t i = 1; i < n; i++) {
    if (a[i - 1] > a[i]) return 0;
  }
  return 1;
}

static int odd3_sorted_ascending(const odd3_t *a, size_t n) {
  for (size_t i = 1; i < n; i++) {
    if (cmp_odd3(&a[i - 1], &a[i]) > 0) return 0;
  }
  return 1;
}

static int kv_sorted_by_key(const kv_t *a, size_t n) {
  for (size_t i = 1; i < n; i++) {
    if (a[i - 1].key > a[i].key) return 0;
  }
  return 1;
}

static unsigned int g_failures = 0;

#define EXPECT(cond, sub_marker)                                              \
  do {                                                                        \
    if (!(cond)) {                                                            \
      printf("TEST:FAIL:clib_qsort:%s\n", sub_marker);                        \
      g_failures++;                                                           \
    } else {                                                                  \
      printf("TEST:PASS:clib_qsort:%s\n", sub_marker);                        \
    }                                                                         \
  } while (0)

/* ----- tests ----------------------------------------------------- */

static void test_empty_no_op(void) {
  int sentinel = 0xdeadbeef;
  /* Should be a no-op; the sentinel must be untouched. */
  qsort(&sentinel, 0, sizeof(int), cmp_int_asc);
  EXPECT(sentinel == (int)0xdeadbeef, "empty_no_op");
}

static void test_single_no_op(void) {
  int one[1] = { 42 };
  qsort(one, 1, sizeof(int), cmp_int_asc);
  EXPECT(one[0] == 42, "single_no_op");
}

static void test_sorted_idempotent(void) {
  int a[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
  size_t n = sizeof(a) / sizeof(a[0]);
  qsort(a, n, sizeof(int), cmp_int_asc);
  EXPECT(int_sorted_ascending(a, n) && a[0] == 1 && a[n - 1] == 12,
         "sorted_idempotent");
}

static void test_reverse_sorted(void) {
  int a[] = { 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1 };
  size_t n = sizeof(a) / sizeof(a[0]);
  qsort(a, n, sizeof(int), cmp_int_asc);
  EXPECT(int_sorted_ascending(a, n) && a[0] == 1 && a[n - 1] == 12,
         "reverse_sorted");
}

static void test_random_ints(void) {
  /* Deterministic "random" sequence — same input every run so the
   * test is reproducible. Hand-picked to mix in/out of order. */
  int a[]   = { 5, 1, 9, 3, 7, 2, 8, 4, 6, 0, 11, 10 };
  int sum_in = 0;
  size_t n = sizeof(a) / sizeof(a[0]);
  for (size_t i = 0; i < n; i++) sum_in += a[i];
  qsort(a, n, sizeof(int), cmp_int_asc);
  int sum_out = 0;
  for (size_t i = 0; i < n; i++) sum_out += a[i];
  EXPECT(int_sorted_ascending(a, n) && sum_in == sum_out, "random_ints");
}

static void test_duplicates_grouped(void) {
  int a[] = { 3, 1, 3, 2, 1, 3, 2, 1, 2, 3, 1, 2 };
  size_t n = sizeof(a) / sizeof(a[0]);
  qsort(a, n, sizeof(int), cmp_int_asc);
  /* Every 1 before every 2 before every 3. */
  int ok = int_sorted_ascending(a, n);
  int ones = 0, twos = 0, threes = 0;
  for (size_t i = 0; i < n; i++) {
    if (a[i] == 1) ones++;
    else if (a[i] == 2) twos++;
    else if (a[i] == 3) threes++;
    else ok = 0;
  }
  EXPECT(ok && ones == 4 && twos == 4 && threes == 4, "duplicates_grouped");
}

static void test_small_under_insertion_threshold(void) {
  /* Below the INSERT_SORT_THRESHOLD (=8) the implementation hands
   * off to insertion-sort. Cover the boundary at n=2..7. */
  int ok = 1;
  for (size_t n = 2; n <= 7; n++) {
    int a[8];
    for (size_t i = 0; i < n; i++) a[i] = (int)(n - i); /* reverse order */
    qsort(a, n, sizeof(int), cmp_int_asc);
    if (!int_sorted_ascending(a, n)) { ok = 0; break; }
    for (size_t i = 0; i < n; i++) {
      if (a[i] != (int)(i + 1)) { ok = 0; break; }
    }
  }
  EXPECT(ok, "small_under_insertion_threshold");
}

static void test_large_pathological_no_overflow(void) {
  /* Reverse-sorted is the canonical pathological case for a naive
   * "always pick last" Lomuto qsort — without median-of-three this
   * would recurse O(n) deep. We rely on the explicit stack +
   * median-of-three to terminate in O(log n) frames. */
  enum { N = 2048 };
  int *a = (int *)malloc(N * sizeof(int));
  if (a == 0) { EXPECT(0, "large_pathological_no_overflow"); return; }
  for (int i = 0; i < N; i++) a[i] = N - i;
  qsort(a, N, sizeof(int), cmp_int_asc);
  int ok = int_sorted_ascending(a, N) && a[0] == 1 && a[N - 1] == N;
  free(a);
  EXPECT(ok, "large_pathological_no_overflow");
}

static void test_struct_elements(void) {
  kv_t a[] = {
    { 5, 50 }, { 1, 10 }, { 4, 40 }, { 2, 20 }, { 3, 30 },
    { 9, 90 }, { 6, 60 }, { 8, 80 }, { 7, 70 }, { 0, 0  },
  };
  size_t n = sizeof(a) / sizeof(a[0]);
  qsort(a, n, sizeof(kv_t), cmp_kv);
  int ok = kv_sorted_by_key(a, n);
  /* Verify payloads moved with their keys (byte-wise swap covers
   * the full struct, not just the key). */
  for (size_t i = 0; i < n; i++) {
    if (a[i].payload != a[i].key * 10) { ok = 0; break; }
  }
  EXPECT(ok, "struct_elements");
}

static void test_byte_elements_size_one(void) {
  unsigned char a[] = { 9, 0, 5, 5, 1, 3, 7, 2, 8, 4, 6, 5 };
  size_t n = sizeof(a) / sizeof(a[0]);
  qsort(a, n, sizeof(unsigned char), cmp_uchar);
  EXPECT(uchar_sorted_ascending(a, n) && a[0] == 0 && a[n - 1] == 9,
         "byte_elements_size_one");
}

static void test_odd_size_unaligned_elements(void) {
  /* 3-byte elements exercise the byte-wise swap on a non-power-of-2
   * width. */
  odd3_t a[] = {
    { { 3, 0, 0 } }, { { 1, 0, 0 } }, { { 2, 0, 0 } },
    { { 5, 0, 0 } }, { { 4, 0, 0 } }, { { 0, 0, 0 } },
    { { 7, 0, 0 } }, { { 6, 0, 0 } }, { { 9, 0, 0 } },
    { { 8, 0, 0 } },
  };
  size_t n = sizeof(a) / sizeof(a[0]);
  qsort(a, n, sizeof(odd3_t), cmp_odd3);
  int ok = odd3_sorted_ascending(a, n);
  for (size_t i = 0; i < n; i++) {
    if (a[i].b[0] != (unsigned char)i) { ok = 0; break; }
  }
  EXPECT(ok, "odd_size_unaligned_elements");
}

/* Model: classic O(n^2) insertion-sort. We compare qsort's output
 * to the model on a varied workload to make sure qsort produces the
 * canonical sorted permutation (not a near-sort). Stability is NOT
 * asserted (qsort is not required to be stable). */
static void model_insertion_sort_int(int *a, size_t n) {
  for (size_t i = 1; i < n; i++) {
    int x = a[i];
    size_t j = i;
    while (j > 0 && a[j - 1] > x) {
      a[j] = a[j - 1];
      j--;
    }
    a[j] = x;
  }
}

static void test_stable_against_model(void) {
  /* Pseudorandom LCG, fixed seed → fully reproducible. */
  enum { N = 257 }; /* prime size, just to exercise odd boundary cases */
  int  a[N], b[N];
  unsigned int seed = 0xC001BABEu;
  for (size_t i = 0; i < N; i++) {
    seed = seed * 1103515245u + 12345u;
    a[i] = (int)((seed >> 8) & 0x3FF);
    b[i] = a[i];
  }
  qsort(a, N, sizeof(int), cmp_int_asc);
  model_insertion_sort_int(b, N);
  int ok = int_sorted_ascending(a, N);
  for (size_t i = 0; i < N; i++) {
    if (a[i] != b[i]) { ok = 0; break; }
  }
  EXPECT(ok, "stable_against_model");
}

/* Drift marker — every shipped symbol from the qsort family must
 * remain reachable through a function pointer. Mirrors the pattern
 * in the str/mem and ctype slices (PRs #416 / #417). */
static void test_symbol_set_pinned(void) {
  void (*pq)(void *, size_t, size_t,
             int (*)(const void *, const void *)) = &qsort;
  /* Use the pointer to satisfy -Wunused. */
  EXPECT(pq != 0, "symbol_set_pinned");
}

int main(void) {
  test_empty_no_op();
  test_single_no_op();
  test_sorted_idempotent();
  test_reverse_sorted();
  test_random_ints();
  test_duplicates_grouped();
  test_small_under_insertion_threshold();
  test_large_pathological_no_overflow();
  test_struct_elements();
  test_byte_elements_size_one();
  test_odd_size_unaligned_elements();
  test_stable_against_model();
  test_symbol_set_pinned();

  if (g_failures == 0) {
    printf("TEST:PASS:clib_qsort\n");
    return 0;
  }
  printf("TEST:FAIL:clib_qsort:failures=%u\n", g_failures);
  return 1;
}
