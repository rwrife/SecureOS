/**
 * @file clib_malloc_test.c
 * @brief Host unit test for the userland heap allocator
 *        (issue #404, plan plans/2026-05-28-in-os-toolchain-self-hosting.md
 *        P1, marker contract `toolchain_heap_isolation`).
 *
 * Covers:
 *   1. Basic alloc/free/realloc/calloc round-trip on a fixed seed window.
 *   2. realloc grow + shrink semantics + content preservation.
 *   3. calloc zero-fill.
 *   4. Boundary-tag coalescing (forward, backward, both).
 *   5. Brk-driven arena growth: seed too small, allocator asks for more.
 *   6. Out-of-arena failure path returns NULL — no panic / no assert.
 *   7. `toolchain_heap_isolation`: re-initialising the allocator after a
 *      first translation unit's allocations behaves exactly like the
 *      first run (no leaked state, no leaked pointers, identical stats).
 *   8. `clib_malloc_min_seed_bytes` agrees with the implementation's
 *      acceptance threshold.
 *
 * Drives the allocator with a host-malloc-backed brk shim — no
 * dependency on `os_mem_*` syscalls (those are the kernel-side
 * follow-up).
 *
 * Launched by:
 *   build/scripts/test_clib_malloc.sh (dispatched via
 *   build/scripts/test.sh clib_malloc).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../user/libs/clib/include/clib/malloc.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:clib_malloc:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* --- brk shim: hands the allocator slices of one host-malloc buffer. --- */

typedef struct {
  uint8_t *buf;
  size_t   capacity;
  size_t   used;       /* bytes already handed out via this shim */
  int      fail_after; /* if >= 0, next brk fails */
} brk_shim_t;

static void *brk_shim_fn(void *ctx, size_t delta) {
  brk_shim_t *s = (brk_shim_t *)ctx;
  if (s->fail_after == 0) {
    return NULL;
  }
  if (s->fail_after > 0) {
    s->fail_after--;
  }
  if (s->used + delta > s->capacity) {
    return NULL;
  }
  void *p = s->buf + s->used;
  s->used += delta;
  return p;
}

/* Align a host-malloc pointer up to 16 bytes by carving an aligned
 * sub-buffer; returns the aligned base + the usable size. */
static uint8_t *align16(uint8_t *p, size_t *sz) {
  uintptr_t a = ((uintptr_t)p + 15u) & ~((uintptr_t)15u);
  size_t lost = (size_t)(a - (uintptr_t)p);
  if (lost > *sz) {
    *sz = 0;
    return NULL;
  }
  *sz -= lost;
  *sz &= ~((size_t)15u);
  return (uint8_t *)a;
}

/* ---------------- tests ---------------- */

static void test_basic_roundtrip(void) {
  uint8_t raw[8192];
  size_t sz = sizeof(raw);
  uint8_t *base = align16(raw, &sz);
  CHECK(clib_malloc_init(base, sz, NULL, NULL) == 0, "basic_roundtrip_init");

  void *a = clib_malloc(64);
  void *b = clib_malloc(128);
  void *c = clib_malloc(256);
  CHECK(a && b && c, "basic_roundtrip_nonnull");
  CHECK(a != b && b != c && a != c, "basic_roundtrip_unique");

  /* Payloads writable. */
  memset(a, 0xAA, 64);
  memset(b, 0xBB, 128);
  memset(c, 0xCC, 256);
  CHECK(((uint8_t *)a)[63] == 0xAA, "basic_roundtrip_a_intact");
  CHECK(((uint8_t *)b)[127] == 0xBB, "basic_roundtrip_b_intact");
  CHECK(((uint8_t *)c)[255] == 0xCC, "basic_roundtrip_c_intact");

  clib_malloc_stats_t st;
  clib_malloc_get_stats(&st);
  CHECK(st.live_alloc_count == 3, "basic_roundtrip_live3");
  CHECK(st.bytes_in_use >= 64 + 128 + 256, "basic_roundtrip_bytes_in_use");

  clib_free(b);
  clib_free(a);
  clib_free(c);
  clib_malloc_get_stats(&st);
  CHECK(st.live_alloc_count == 0, "basic_roundtrip_drained");

  fprintf(stdout, "TEST:PASS:clib_malloc:basic_roundtrip\n");
  clib_malloc_shutdown();
}

static void test_realloc(void) {
  uint8_t raw[8192];
  size_t sz = sizeof(raw);
  uint8_t *base = align16(raw, &sz);
  CHECK(clib_malloc_init(base, sz, NULL, NULL) == 0, "realloc_init");

  uint8_t *p = (uint8_t *)clib_malloc(32);
  CHECK(p != NULL, "realloc_initial");
  for (int i = 0; i < 32; i++) {
    p[i] = (uint8_t)i;
  }

  /* Grow. */
  uint8_t *q = (uint8_t *)clib_realloc(p, 128);
  CHECK(q != NULL, "realloc_grow_nonnull");
  for (int i = 0; i < 32; i++) {
    CHECK(q[i] == (uint8_t)i, "realloc_grow_preserved");
  }

  /* Shrink — implementation may return same pointer. */
  uint8_t *r = (uint8_t *)clib_realloc(q, 16);
  CHECK(r != NULL, "realloc_shrink_nonnull");
  for (int i = 0; i < 16; i++) {
    CHECK(r[i] == (uint8_t)i, "realloc_shrink_preserved");
  }

  /* realloc(NULL, n) == malloc(n). */
  void *fresh = clib_realloc(NULL, 48);
  CHECK(fresh != NULL, "realloc_null_is_malloc");

  /* realloc(p, 0) == free(p). */
  void *gone = clib_realloc(r, 0);
  CHECK(gone == NULL, "realloc_zero_is_free");

  clib_free(fresh);
  fprintf(stdout, "TEST:PASS:clib_malloc:realloc_growth_and_shrink\n");
  clib_malloc_shutdown();
}

static void test_calloc(void) {
  uint8_t raw[4096];
  size_t sz = sizeof(raw);
  uint8_t *base = align16(raw, &sz);
  CHECK(clib_malloc_init(base, sz, NULL, NULL) == 0, "calloc_init");

  uint8_t *p = (uint8_t *)clib_calloc(64, 4);
  CHECK(p != NULL, "calloc_nonnull");
  for (int i = 0; i < 64 * 4; i++) {
    CHECK(p[i] == 0, "calloc_zeroed");
  }
  clib_free(p);

  /* Overflow guard. */
  void *overflow = clib_calloc((size_t)-1, 2);
  CHECK(overflow == NULL, "calloc_overflow_null");

  fprintf(stdout, "TEST:PASS:clib_malloc:calloc_zeroes\n");
  clib_malloc_shutdown();
}

static void test_coalesce(void) {
  uint8_t raw[8192];
  size_t sz = sizeof(raw);
  uint8_t *base = align16(raw, &sz);
  CHECK(clib_malloc_init(base, sz, NULL, NULL) == 0, "coalesce_init");

  void *a = clib_malloc(256);
  void *b = clib_malloc(256);
  void *c = clib_malloc(256);
  CHECK(a && b && c, "coalesce_setup");

  /* Free middle first; then either neighbour; coalesce should fold all. */
  clib_free(b);
  clib_free(a);
  clib_free(c);

  /* After draining we should be able to allocate something close to
   * the original arena size again — proving coalescing collapsed the
   * fragmented free list. */
  void *big = clib_malloc(sz - 256);
  CHECK(big != NULL, "coalesce_recombined_into_big_block");
  clib_free(big);

  fprintf(stdout, "TEST:PASS:clib_malloc:coalesce_neighbours\n");
  clib_malloc_shutdown();
}

static void test_brk_growth(void) {
  /* Tiny seed (just past the minimum) + a brk-backed buffer. */
  uint8_t backing[16384];
  size_t bsz = sizeof(backing);
  uint8_t *bbase = align16(backing, &bsz);
  brk_shim_t shim = { .buf = bbase, .capacity = bsz, .used = 0, .fail_after = -1 };

  CHECK(clib_malloc_init(NULL, 0, brk_shim_fn, &shim) == 0,
        "brk_growth_init_empty_seed");

  /* First malloc must trigger a brk and succeed. */
  void *a = clib_malloc(1024);
  CHECK(a != NULL, "brk_growth_first_alloc");
  CHECK(shim.used > 0, "brk_growth_shim_consumed");

  /* Many more allocs should keep growing the shim. */
  void *ptrs[8];
  for (int i = 0; i < 8; i++) {
    ptrs[i] = clib_malloc(512);
    CHECK(ptrs[i] != NULL, "brk_growth_repeat_alloc");
  }
  for (int i = 0; i < 8; i++) {
    clib_free(ptrs[i]);
  }
  clib_free(a);

  fprintf(stdout, "TEST:PASS:clib_malloc:brk_growth\n");
  clib_malloc_shutdown();
}

static void test_out_of_arena_no_panic(void) {
  /* Seed + brk that fails on the first call. The allocator MUST return
   * NULL and not crash / not assert. */
  uint8_t raw[1024];
  size_t sz = sizeof(raw);
  uint8_t *base = align16(raw, &sz);
  brk_shim_t shim = { .buf = NULL, .capacity = 0, .used = 0, .fail_after = 0 };

  CHECK(clib_malloc_init(base, sz, brk_shim_fn, &shim) == 0,
        "oof_init");

  /* Drain the seed. */
  void *small = clib_malloc(sz / 2);
  CHECK(small != NULL, "oof_drain_first");

  /* Now ask for far more than the arena can hold; brk will fail. */
  void *huge = clib_malloc(1024 * 1024);
  CHECK(huge == NULL, "oof_huge_alloc_returns_null");

  clib_free(small);
  fprintf(stdout, "TEST:PASS:clib_malloc:out_of_arena_no_panic\n");
  clib_malloc_shutdown();
}

static void test_toolchain_heap_isolation(void) {
  /* M7 plan acceptance: the same allocator instance, re-initialised
   * between two translation units, must behave identically — no state
   * leaks across runs. We assert stats parity between run 1 and run 2. */

  uint8_t raw[8192];
  size_t sz = sizeof(raw);
  uint8_t *base = align16(raw, &sz);

  clib_malloc_stats_t s1;
  clib_malloc_stats_t s2;

  /* Run 1. */
  CHECK(clib_malloc_init(base, sz, NULL, NULL) == 0, "iso_init1");
  void *r1a = clib_malloc(123);
  void *r1b = clib_malloc(456);
  void *r1c = clib_malloc(789);
  CHECK(r1a && r1b && r1c, "iso_run1_allocs");
  /* Intentionally do NOT free — simulates a TU that "leaks" by design
   * because the process is about to exit. The allocator must still be
   * resettable. */
  clib_malloc_get_stats(&s1);

  /* Reset for run 2. */
  CHECK(clib_malloc_init(base, sz, NULL, NULL) == 0, "iso_init2");
  /* Stats must report a clean slate post-init. */
  clib_malloc_stats_t fresh;
  clib_malloc_get_stats(&fresh);
  CHECK(fresh.bytes_in_use == 0, "iso_post_init_zero_in_use");
  CHECK(fresh.live_alloc_count == 0, "iso_post_init_zero_live");

  /* Run 2 — same exact allocation pattern. */
  void *r2a = clib_malloc(123);
  void *r2b = clib_malloc(456);
  void *r2c = clib_malloc(789);
  CHECK(r2a && r2b && r2c, "iso_run2_allocs");
  clib_malloc_get_stats(&s2);

  /* Determinism: byte-identical stats between the two runs proves no
   * state leaked across init. */
  CHECK(s1.bytes_in_use == s2.bytes_in_use, "iso_bytes_in_use_match");
  CHECK(s1.live_alloc_count == s2.live_alloc_count, "iso_live_count_match");
  CHECK(s1.bytes_total == s2.bytes_total, "iso_bytes_total_match");

  /* Pointers handed out in run 2 should equal those from run 1 —
   * deterministic placement is what makes the toolchain reproducible. */
  CHECK(r2a == r1a, "iso_pointer_parity_a");
  CHECK(r2b == r1b, "iso_pointer_parity_b");
  CHECK(r2c == r1c, "iso_pointer_parity_c");

  fprintf(stdout, "TEST:PASS:clib_malloc:toolchain_heap_isolation\n");
  clib_malloc_shutdown();
}

static void test_min_seed_bytes_anchored(void) {
  size_t min = clib_malloc_min_seed_bytes();
  CHECK(min > 0, "min_seed_positive");
  CHECK((min & 15u) == 0, "min_seed_16b_aligned");

  /* A seed smaller than the threshold must be rejected. */
  uint8_t small[16];
  /* `small` is at most 16 bytes; min is > 16 by construction. */
  int rc = clib_malloc_init(small, 16, NULL, NULL);
  CHECK(rc != 0, "min_seed_rejects_undersized");

  /* A NULL seed with positive size is rejected. */
  rc = clib_malloc_init(NULL, 1024, NULL, NULL);
  CHECK(rc != 0, "min_seed_rejects_null_with_size");

  /* A misaligned seed is rejected. */
  uint8_t mis[2048];
  rc = clib_malloc_init(mis + 1, 1024, NULL, NULL);
  CHECK(rc != 0, "min_seed_rejects_misaligned");

  fprintf(stdout, "TEST:PASS:clib_malloc:min_seed_bytes_anchored\n");
  clib_malloc_shutdown();
}

int main(void) {
  test_basic_roundtrip();
  test_realloc();
  test_calloc();
  test_coalesce();
  test_brk_growth();
  test_out_of_arena_no_panic();
  test_toolchain_heap_isolation();
  test_min_seed_bytes_anchored();

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:clib_malloc\n");
    return 1;
  }
  fprintf(stdout, "TEST:PASS:clib_malloc\n");
  return 0;
}
