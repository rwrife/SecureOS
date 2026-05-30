/*
 * tests/clib_bsearch_test.c
 *
 * Host unit test for the freestanding `bsearch` shipped by
 * `user/libs/clib/src/bsearch.c` (issue #407 / M7-TOOLCHAIN-004
 * slice 7, plan
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3).
 *
 * Compiled with `-fno-builtin` (see
 * `build/scripts/test_clib_bsearch.sh`) so the assertions exercise
 * OUR `bsearch`, not `__builtin_bsearch` / any host libc shortcut.
 *
 * Sub-markers (each must round-trip via
 * `TEST:PASS:clib_bsearch:...`):
 *   - empty_returns_null
 *   - single_hit
 *   - single_miss
 *   - hit_at_first
 *   - hit_at_last
 *   - hit_in_middle
 *   - miss_below_range
 *   - miss_above_range
 *   - miss_between_neighbours
 *   - duplicates_returns_some_match
 *   - struct_elements_payload_intact
 *   - odd_size_unaligned_elements
 *   - large_array_no_overflow
 *   - defensive_null_key
 *   - defensive_null_compar
 *   - defensive_zero_size
 *   - defensive_null_base_nonzero_nmemb
 *   - symbol_set_pinned
 *
 * Roll-up marker:
 *   - TEST:PASS:clib_bsearch     (only emitted if every sub-marker
 *                                 passed and zero TEST:FAIL: lines
 *                                 were recorded).
 */

#include <stdio.h>

/* Do NOT include <stdlib.h>: the hosted prototype carries
 * __attribute__((nonnull)) on every pointer argument, which would
 * make the defensive-NULL tests below trip -Werror=nonnull at
 * compile time. Our own <clib/bsearch.h> declares the same
 * canonical signature without that attribute, which is what we
 * want both for the test and for the freestanding contract. */

#include "../user/libs/clib/include/clib/bsearch.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:clib_bsearch:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

#define PASS(name) printf("TEST:PASS:clib_bsearch:%s\n", (name))

/* ---- compare functions ----------------------------------------- */

static int cmp_int_asc(const void *a, const void *b) {
  int ia = *(const int *)a;
  int ib = *(const int *)b;
  if (ia < ib) return -1;
  if (ia > ib) return 1;
  return 0;
}

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

/* ---- tests ----------------------------------------------------- */

static void test_empty_returns_null(void) {
  int key = 7;
  int arr[1] = { 42 };
  void *r1 = bsearch(&key, arr, 0, sizeof(int), cmp_int_asc);
  /* nmemb==0 with non-NULL base is the canonical "empty array" case. */
  void *r2 = bsearch(&key, NULL, 0, sizeof(int), cmp_int_asc);
  CHECK(r1 == NULL, "empty_returns_null");
  CHECK(r2 == NULL, "empty_returns_null");
  if (r1 == NULL && r2 == NULL) PASS("empty_returns_null");
}

static void test_single_hit(void) {
  int arr[1] = { 42 };
  int key = 42;
  int *p = (int *)bsearch(&key, arr, 1, sizeof(int), cmp_int_asc);
  CHECK(p == &arr[0], "single_hit");
  if (p == &arr[0]) PASS("single_hit");
}

static void test_single_miss(void) {
  int arr[1] = { 42 };
  int key = 7;
  int *p = (int *)bsearch(&key, arr, 1, sizeof(int), cmp_int_asc);
  CHECK(p == NULL, "single_miss");
  if (p == NULL) PASS("single_miss");
}

static void test_hit_at_first(void) {
  int arr[] = { 1, 3, 5, 7, 9, 11, 13, 15 };
  int key = 1;
  int *p = (int *)bsearch(&key, arr, sizeof(arr)/sizeof(arr[0]),
                          sizeof(int), cmp_int_asc);
  CHECK(p != NULL && *p == 1 && p == &arr[0], "hit_at_first");
  if (p != NULL && *p == 1 && p == &arr[0]) PASS("hit_at_first");
}

static void test_hit_at_last(void) {
  int arr[] = { 1, 3, 5, 7, 9, 11, 13, 15 };
  int key = 15;
  size_t n = sizeof(arr)/sizeof(arr[0]);
  int *p = (int *)bsearch(&key, arr, n, sizeof(int), cmp_int_asc);
  CHECK(p != NULL && *p == 15 && p == &arr[n-1], "hit_at_last");
  if (p != NULL && *p == 15 && p == &arr[n-1]) PASS("hit_at_last");
}

static void test_hit_in_middle(void) {
  int arr[] = { 1, 3, 5, 7, 9, 11, 13, 15 };
  int key = 7;
  int *p = (int *)bsearch(&key, arr, sizeof(arr)/sizeof(arr[0]),
                          sizeof(int), cmp_int_asc);
  CHECK(p != NULL && *p == 7, "hit_in_middle");
  if (p != NULL && *p == 7) PASS("hit_in_middle");
}

static void test_miss_below_range(void) {
  int arr[] = { 10, 20, 30, 40, 50 };
  int key = 0;
  int *p = (int *)bsearch(&key, arr, sizeof(arr)/sizeof(arr[0]),
                          sizeof(int), cmp_int_asc);
  CHECK(p == NULL, "miss_below_range");
  if (p == NULL) PASS("miss_below_range");
}

static void test_miss_above_range(void) {
  int arr[] = { 10, 20, 30, 40, 50 };
  int key = 100;
  int *p = (int *)bsearch(&key, arr, sizeof(arr)/sizeof(arr[0]),
                          sizeof(int), cmp_int_asc);
  CHECK(p == NULL, "miss_above_range");
  if (p == NULL) PASS("miss_above_range");
}

static void test_miss_between_neighbours(void) {
  int arr[] = { 10, 20, 30, 40, 50 };
  int key = 25;
  int *p = (int *)bsearch(&key, arr, sizeof(arr)/sizeof(arr[0]),
                          sizeof(int), cmp_int_asc);
  CHECK(p == NULL, "miss_between_neighbours");
  if (p == NULL) PASS("miss_between_neighbours");
}

static void test_duplicates_returns_some_match(void) {
  /* C standard does not pin which equal element is returned; the
   * contract is "returns a pointer to a matching element". */
  int arr[] = { 1, 2, 2, 2, 2, 2, 3 };
  int key = 2;
  size_t n = sizeof(arr)/sizeof(arr[0]);
  int *p = (int *)bsearch(&key, arr, n, sizeof(int), cmp_int_asc);
  int ok = (p != NULL) && (*p == 2) &&
           (p >= &arr[1] && p <= &arr[5]);
  CHECK(ok, "duplicates_returns_some_match");
  if (ok) PASS("duplicates_returns_some_match");
}

static void test_struct_elements_payload_intact(void) {
  /* The byte-wise pointer arithmetic must locate the WHOLE struct,
   * not just the key field — so we verify the payload travels with
   * the returned pointer. */
  kv_t arr[] = {
    { 1, 100 }, { 3, 300 }, { 5, 500 }, { 7, 700 }, { 9, 900 },
  };
  kv_t key = { 5, 0 };
  size_t n = sizeof(arr)/sizeof(arr[0]);
  kv_t *p = (kv_t *)bsearch(&key, arr, n, sizeof(kv_t), cmp_kv);
  int ok = (p != NULL) && (p->key == 5) && (p->payload == 500);
  CHECK(ok, "struct_elements_payload_intact");
  if (ok) PASS("struct_elements_payload_intact");
}

static void test_odd_size_unaligned_elements(void) {
  /* 3-byte element width: stresses the byte-wise pointer arithmetic
   * on a stride that the compiler cannot collapse to a word-sized
   * load. Same shape as the qsort slice's odd_size case. */
  odd3_t arr[6] = {
    {{0x01, 0x02, 0x03}},
    {{0x04, 0x05, 0x06}},
    {{0x07, 0x08, 0x09}},
    {{0x0a, 0x0b, 0x0c}},
    {{0x0d, 0x0e, 0x0f}},
    {{0x10, 0x11, 0x12}},
  };
  odd3_t key_hit  = {{0x0a, 0x0b, 0x0c}};
  odd3_t key_miss = {{0x0a, 0x0b, 0x0d}};
  odd3_t *p_hit  = (odd3_t *)bsearch(&key_hit,  arr, 6, sizeof(odd3_t), cmp_odd3);
  odd3_t *p_miss = (odd3_t *)bsearch(&key_miss, arr, 6, sizeof(odd3_t), cmp_odd3);
  int ok = (p_hit == &arr[3]) && (p_miss == NULL);
  CHECK(ok, "odd_size_unaligned_elements");
  if (ok) PASS("odd_size_unaligned_elements");
}

static void test_large_array_no_overflow(void) {
  /* Sized to cross the deep-recursion threshold the qsort slice
   * uses (N=2048); also exercises the overflow-safe midpoint
   * `lo + (hi-lo)/2`. */
  enum { N = 2048 };
  static int arr[N];
  for (int i = 0; i < N; i++) arr[i] = i * 2;  /* sorted, gap = 2 */

  /* Every even key in range must hit; every odd key must miss. */
  int hits_ok = 1, miss_ok = 1;
  for (int k = 0; k < N * 2; k++) {
    int *p = (int *)bsearch(&k, arr, N, sizeof(int), cmp_int_asc);
    if ((k & 1) == 0) {
      if (p == NULL || *p != k) { hits_ok = 0; break; }
    } else {
      if (p != NULL) { miss_ok = 0; break; }
    }
  }
  /* Out-of-range probes. */
  int below = -1, above = N * 2 + 7;
  int *pb = (int *)bsearch(&below, arr, N, sizeof(int), cmp_int_asc);
  int *pa = (int *)bsearch(&above, arr, N, sizeof(int), cmp_int_asc);
  int ok = hits_ok && miss_ok && pb == NULL && pa == NULL;
  CHECK(ok, "large_array_no_overflow");
  if (ok) PASS("large_array_no_overflow");
}

static void test_defensive_null_key(void) {
  int arr[] = { 1, 2, 3 };
  void *p = bsearch(NULL, arr, 3, sizeof(int), cmp_int_asc);
  CHECK(p == NULL, "defensive_null_key");
  if (p == NULL) PASS("defensive_null_key");
}

static void test_defensive_null_compar(void) {
  int arr[] = { 1, 2, 3 };
  int key = 2;
  void *p = bsearch(&key, arr, 3, sizeof(int), NULL);
  CHECK(p == NULL, "defensive_null_compar");
  if (p == NULL) PASS("defensive_null_compar");
}

static void test_defensive_zero_size(void) {
  int arr[] = { 1, 2, 3 };
  int key = 2;
  void *p = bsearch(&key, arr, 3, 0, cmp_int_asc);
  CHECK(p == NULL, "defensive_zero_size");
  if (p == NULL) PASS("defensive_zero_size");
}

static void test_defensive_null_base_nonzero_nmemb(void) {
  int key = 2;
  /* base==NULL with nmemb>0 is caller-UB. The slice promises a
   * defensive NULL rather than a deref. */
  void *p = bsearch(&key, NULL, 3, sizeof(int), cmp_int_asc);
  CHECK(p == NULL, "defensive_null_base_nonzero_nmemb");
  if (p == NULL) PASS("defensive_null_base_nonzero_nmemb");
}

/* ---- symbol set pinned ----------------------------------------- */

/*
 * Drift guard: take the address of every shipped symbol and call
 * through it. A future TinyCC drop / unrelated PR cannot silently
 * remove a family member without flipping this marker to FAIL.
 *
 * Slice 7 ships exactly one symbol (`bsearch`). The pin is still
 * valuable: it freezes the SPELLING (canonical libc name, no
 * `clib_` prefix) so any rename in a future refactor breaks the
 * bundle before TinyCC's link does.
 */
static void test_symbol_set_pinned(void) {
  void *(*fn)(const void *, const void *, size_t, size_t,
              int (*)(const void *, const void *)) = bsearch;
  int arr[3] = { 10, 20, 30 };
  int key = 20;
  int *p = (int *)fn(&key, arr, 3, sizeof(int), cmp_int_asc);
  int ok = (p != NULL) && (*p == 20);
  CHECK(ok, "symbol_set_pinned");
  if (ok) PASS("symbol_set_pinned");
}

int main(void) {
  test_empty_returns_null();
  test_single_hit();
  test_single_miss();
  test_hit_at_first();
  test_hit_at_last();
  test_hit_in_middle();
  test_miss_below_range();
  test_miss_above_range();
  test_miss_between_neighbours();
  test_duplicates_returns_some_match();
  test_struct_elements_payload_intact();
  test_odd_size_unaligned_elements();
  test_large_array_no_overflow();
  test_defensive_null_key();
  test_defensive_null_compar();
  test_defensive_zero_size();
  test_defensive_null_base_nonzero_nmemb();
  test_symbol_set_pinned();

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:clib_bsearch\n");
    return 1;
  }
  /* Roll-up marker: emitted iff every sub-marker passed and zero
   * TEST:FAIL: lines were recorded. */
  printf("TEST:PASS:clib_bsearch\n");
  return 0;
}
