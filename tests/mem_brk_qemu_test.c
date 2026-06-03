/**
 * @file mem_brk_qemu_test.c
 * @brief M7-TOOLCHAIN-001 `_qemu`-tier end-to-end round-trip for the
 *        `os_mem_brk` bridge slot (issue #495, follow-up to #421).
 *
 * Closes the QEMU peer that `docs/abi/syscalls.md` §`os_mem_brk` flagged
 * as a deliberate follow-up once the `clib_brk_fn` forwarder
 * (`user/libs/clib/include/clib/os_brk.h`, PR #455) landed.
 *
 * The pre-existing host link-pin (`tests/mem_brk_wrapper_test.c`) only
 * exercises the user-runtime wrapper's signature + NULL-out guard; it
 * cannot drive the bridge implementation because the wrapper reads
 * through the fixed bridge address `SECUREOS_NATIVE_BRIDGE_ADDR`,
 * which is unmapped on bare host. This peer instead links the
 * production `app_native_mem_brk` body directly (extracted into
 * `kernel/user/app_native_heap.c` for this slice) and drives the
 * same `(int delta, void **out_prev_break) -> int` contract the
 * launcher wires into `bridge->mem_brk`.
 *
 * Following the BUILD_ROADMAP §5.2 `_qemu` convention used by the
 * M2/M3/M4/M5 peers (`tests/m2_helloapp_*_qemu_test.c`,
 * `tests/m3_fs_*_qemu_test.c`, …) the test is a pure host build.
 *
 * Markers (consumed by `build/scripts/test_mem_brk_qemu.sh`):
 *
 *   TEST:PASS:mem_brk_qemu:grow
 *   TEST:PASS:mem_brk_qemu:shrink
 *   TEST:PASS:mem_brk_qemu:over_cap_denied
 *   TEST:PASS:mem_brk_qemu:arena_reset
 *   TEST:PASS:mem_brk_qemu
 *
 * The four sub-markers mirror the names called out in the issue body
 * so the marker spelling is a single source of truth pinned across
 * the doc / test / script trio (same pattern as
 * `launcher_arena_bytes`).
 *
 * Issue: #495. Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md §P1.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/user/app_native_heap.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:mem_brk_qemu:%s\n", reason);
  g_fail = 1;
}

/* OS_STATUS_* mirror — kept local so the test does not depend on the
 * user-runtime header (the wrapper's NULL-bridge dereference would
 * fire if we accidentally called the runtime path here). */
enum {
  STATUS_OK = 0,
  STATUS_DENIED = 1,
  STATUS_ERROR = 3,
};

/* --------------------------------------------------------------
 * Sub-test 1: grow — sbrk(+page) twice, sentinel survives.
 *
 * Covers the sbrk(2)-shape contract: return value is the *previous*
 * break, the newly committed bytes are writable, and a second growth
 * does not clobber the prior page (contiguous arena, no copy).
 * -------------------------------------------------------------- */
static void test_grow(void) {
  app_native_heap_reset();

  void *prev1 = NULL;
  if (app_native_mem_brk(4096, &prev1) != STATUS_OK) {
    fail("grow_first_returned_non_ok");
    return;
  }
  if (prev1 == NULL) {
    fail("grow_first_prev_break_null");
    return;
  }
  /* The first growth from break=0 must return the pool base. */
  if (app_native_heap_break_for_tests() != 4096u) {
    fail("grow_first_break_not_advanced");
    return;
  }

  /* Write a sentinel into the freshly committed page. */
  volatile uint8_t *page1 = (uint8_t *)prev1;
  for (int i = 0; i < 4096; i++) {
    page1[i] = (uint8_t)(i & 0xFF);
  }

  void *prev2 = NULL;
  if (app_native_mem_brk(4096, &prev2) != STATUS_OK) {
    fail("grow_second_returned_non_ok");
    return;
  }
  /* The second growth's "previous break" must equal the END of the
   * first commit (sbrk contiguity). */
  if (prev2 != (void *)((uint8_t *)prev1 + 4096)) {
    fail("grow_second_not_contiguous");
    return;
  }
  if (app_native_heap_break_for_tests() != 8192u) {
    fail("grow_second_break_not_advanced");
    return;
  }

  /* Sentinel from page 1 must survive — second growth cannot remap or
   * zero the first page. */
  for (int i = 0; i < 4096; i++) {
    if (page1[i] != (uint8_t)(i & 0xFF)) {
      fail("grow_sentinel_clobbered");
      return;
    }
  }

  /* Write into page 2 (proves it is also writable / committed). */
  volatile uint8_t *page2 = (uint8_t *)prev2;
  for (int i = 0; i < 4096; i++) {
    page2[i] = (uint8_t)((i + 0x80) & 0xFF);
  }

  printf("TEST:PASS:mem_brk_qemu:grow\n");
}

/* --------------------------------------------------------------
 * Sub-test 2: shrink — sbrk(-page) moves break back; prior bytes
 * past the new break are no longer in the active arena.
 * -------------------------------------------------------------- */
static void test_shrink(void) {
  app_native_heap_reset();

  void *p = NULL;
  if (app_native_mem_brk(8192, &p) != STATUS_OK) {
    fail("shrink_setup_grow_failed");
    return;
  }
  if (app_native_heap_break_for_tests() != 8192u) {
    fail("shrink_setup_break_wrong");
    return;
  }

  void *prev = NULL;
  if (app_native_mem_brk(-4096, &prev) != STATUS_OK) {
    fail("shrink_returned_non_ok");
    return;
  }
  /* sbrk(2): the return on negative delta is also the previous
   * break (the byte *after* the last committed byte before shrink). */
  if (prev != (void *)((uint8_t *)p + 8192)) {
    fail("shrink_prev_break_wrong");
    return;
  }
  if (app_native_heap_break_for_tests() != 4096u) {
    fail("shrink_break_not_rewound");
    return;
  }

  /* Shrink past the base must deny cleanly (no panic, no
   * underflow). */
  void *under = NULL;
  if (app_native_mem_brk(-(int)8192, &under) != STATUS_DENIED) {
    fail("shrink_below_base_not_denied");
    return;
  }
  /* Deny must NOT have moved the break. */
  if (app_native_heap_break_for_tests() != 4096u) {
    fail("shrink_deny_corrupted_break");
    return;
  }

  printf("TEST:PASS:mem_brk_qemu:shrink\n");
}

/* --------------------------------------------------------------
 * Sub-test 3: over_cap_denied — growth past APP_NATIVE_HEAP_BYTES
 * returns OS_STATUS_DENIED with no panic and no break movement.
 *
 * This is the "deny-clean" half of the M7-TOOLCHAIN-001 contract
 * (`docs/abi/syscalls.md` §`os_mem_brk`: "Out-of-arena growth
 * returns OS_STATUS_DENIED and does NOT move the break or panic
 * the kernel"). The userland allocator (`clib_malloc`) relies on
 * this so a failing `malloc` returns NULL instead of crashing.
 * -------------------------------------------------------------- */
static void test_over_cap_denied(void) {
  app_native_heap_reset();

  unsigned long cap = (unsigned long)APP_NATIVE_HEAP_BYTES;
  if (cap == 0 || cap > 0x7FFFFFFFul) {
    fail("over_cap_pool_size_implausible");
    return;
  }

  /* INT_MAX-shaped growth must deny (and not overflow the int +
   * size_t arithmetic in the bounds check). */
  void *p = NULL;
  if (app_native_mem_brk(0x7FFFFFFF, &p) != STATUS_DENIED) {
    fail("over_cap_intmax_grow_not_denied");
    return;
  }
  if (app_native_heap_break_for_tests() != 0u) {
    fail("over_cap_intmax_break_moved");
    return;
  }

  /* Exact pool-size+1 deny (only viable if the pool fits in int). */
  if (cap < (unsigned long)0x7FFFFFFFul) {
    int over = (int)cap + 1;
    if (over > 0) {
      p = NULL;
      if (app_native_mem_brk(over, &p) != STATUS_DENIED) {
        fail("over_cap_plus_one_not_denied");
        return;
      }
      if (app_native_heap_break_for_tests() != 0u) {
        fail("over_cap_plus_one_break_moved");
        return;
      }
    }
  }

  /* After a deny, a normal-sized grow must still succeed — the deny
   * path must not have corrupted the allocator state. */
  void *ok_prev = NULL;
  if (app_native_mem_brk(4096, &ok_prev) != STATUS_OK) {
    fail("over_cap_post_deny_grow_failed");
    return;
  }

  /* NULL out-pointer must return OS_STATUS_ERROR without panic
   * (the contract the wrapper short-circuits at the host layer). */
  if (app_native_mem_brk(0, NULL) != STATUS_ERROR) {
    fail("over_cap_null_out_not_error");
    return;
  }

  printf("TEST:PASS:mem_brk_qemu:over_cap_denied\n");
}

/* --------------------------------------------------------------
 * Sub-test 4: arena_reset — `app_native_heap_reset()` is what the
 * launcher's `!nested` bridge install path calls so a fresh
 * top-level app starts at break=0 (the per-process arena-isolation
 * half of `toolchain_heap_isolation`). Simulate two back-to-back
 * "spawns" and assert app B's first brk-query returns offset 0.
 * -------------------------------------------------------------- */
static void test_arena_reset(void) {
  app_native_heap_reset();

  /* App A: grow to a non-trivial size, write a sentinel at the
   * tail. */
  void *a_prev = NULL;
  if (app_native_mem_brk(16384, &a_prev) != STATUS_OK) {
    fail("reset_app_a_grow_failed");
    return;
  }
  volatile uint8_t *a_tail = (uint8_t *)a_prev + 16384 - 8;
  for (int i = 0; i < 8; i++) {
    a_tail[i] = (uint8_t)(0xA0 + i);
  }
  if (app_native_heap_break_for_tests() != 16384u) {
    fail("reset_app_a_break_wrong");
    return;
  }

  /* Simulated teardown + fresh-launcher path. */
  app_native_heap_reset();

  if (app_native_heap_break_for_tests() != 0u) {
    fail("reset_break_not_zero_after_reset");
    return;
  }

  /* App B: first call must report previous-break at the pool base
   * (offset 0). The pool reuse is intentional (in-tree native
   * runtime is a single shared BSS pool gated by reset), but the
   * arena window B sees MUST start fresh — this is what the
   * `toolchain_heap_isolation` acceptance marker pins. */
  void *b_prev = NULL;
  if (app_native_mem_brk(0, &b_prev) != STATUS_OK) {
    fail("reset_app_b_query_failed");
    return;
  }
  if (b_prev != a_prev) {
    /* Same pool base = same address; the reset only rewound the
     * break offset. This is the intentional in-tree shape. */
    fail("reset_app_b_pool_base_drifted");
    return;
  }

  void *b_grow = NULL;
  if (app_native_mem_brk(4096, &b_grow) != STATUS_OK) {
    fail("reset_app_b_grow_failed");
    return;
  }
  if (b_grow != a_prev) {
    fail("reset_app_b_grow_did_not_start_at_base");
    return;
  }
  if (app_native_heap_break_for_tests() != 4096u) {
    fail("reset_app_b_break_wrong");
    return;
  }

  printf("TEST:PASS:mem_brk_qemu:arena_reset\n");
}

int main(void) {
  printf("TEST:START:mem_brk_qemu\n");

  test_grow();
  test_shrink();
  test_over_cap_denied();
  test_arena_reset();

  if (g_fail) {
    printf("TEST:FAIL:mem_brk_qemu\n");
    return 1;
  }
  printf("TEST:PASS:mem_brk_qemu\n");
  return 0;
}
