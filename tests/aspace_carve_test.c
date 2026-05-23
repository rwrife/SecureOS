/**
 * @file aspace_carve_test.c
 * @brief Host-side unit tests for kernel/proc/address_space.c
 *        (issue #248, plan #198 slice 2).
 *
 * Covers the invariants spelled out in the issue:
 *
 *   1. Partitioning a 1 MiB-class arena into N windows yields N
 *      non-overlapping, equal-sized address spaces with strictly
 *      increasing `base`, every `stack_top` inside its own window,
 *      every `ipc_scratch` inside its own window, and `pt_reserved`
 *      always NULL.
 *   2. Arena too small for the requested count → ASPACE_ERR_ARENA_TOO_SMALL.
 *   3. NULL `out` or zero `count` → ASPACE_ERR_INVALID_ARG.
 *   4. Field-order / size invariants on `address_space_t` are pinned via
 *      compile-time `_Static_assert`s so future struct churn is loud.
 *
 * Launched by:
 *   build/scripts/test_aspace_carve.sh (dispatched via
 *   build/scripts/test.sh aspace_carve).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/proc/address_space.h"
#include "../kernel/ipc/ipc_msg.h"

/* Layout freeze: any reorder is a deliberate v0 ABI-of-record change. */
_Static_assert(offsetof(address_space_t, base) == 0,
               "address_space_t.base must be the first field");
_Static_assert(offsetof(address_space_t, size) > offsetof(address_space_t, base),
               "address_space_t.size must follow .base");
_Static_assert(offsetof(address_space_t, stack_top) > offsetof(address_space_t, size),
               "address_space_t.stack_top must follow .size");
_Static_assert(offsetof(address_space_t, ipc_scratch) > offsetof(address_space_t, stack_top),
               "address_space_t.ipc_scratch must follow .stack_top");
_Static_assert(offsetof(address_space_t, pt_reserved) > offsetof(address_space_t, ipc_scratch),
               "address_space_t.pt_reserved must follow .ipc_scratch");

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* A simple backing buffer "arena" sitting in the host process address
 * space — the partitioner only manipulates the [base,base+size) triple,
 * it never dereferences ipc_scratch, so this is fine for unit tests. */
static uint8_t g_arena[1u * 1024u * 1024u];

static void test_partition_layout_invariants(void) {
  const size_t N = 4u;
  address_space_t spaces[8];
  memset(spaces, 0xCC, sizeof(spaces));

  aspace_result_t r = aspace_partition((uintptr_t)g_arena,
                                       sizeof(g_arena),
                                       spaces,
                                       N);
  CHECK(r == ASPACE_OK, "aspace_partition_ok_returned");

  size_t per = spaces[0].size;
  CHECK(per >= aspace_window_min_bytes(), "aspace_partition_window_min");

  for (size_t i = 0; i < N; ++i) {
    CHECK(spaces[i].size == per, "aspace_partition_equal_size");
    CHECK(spaces[i].base == (uintptr_t)g_arena + per * i,
          "aspace_partition_base_progression");
    CHECK(spaces[i].stack_top == spaces[i].base + per,
          "aspace_partition_stack_top_inside");
    CHECK(spaces[i].ipc_scratch == (uint8_t *)spaces[i].base,
          "aspace_partition_scratch_inside");
    CHECK(spaces[i].pt_reserved == NULL, "aspace_partition_pt_reserved_null");

    if (i > 0) {
      CHECK(spaces[i].base > spaces[i - 1].base,
            "aspace_partition_base_monotonic");
      CHECK(spaces[i].base >= spaces[i - 1].stack_top
            || spaces[i - 1].stack_top <= spaces[i].base,
            "aspace_partition_no_overlap");
    }
  }

  /* Slots beyond N must remain untouched (sentinel 0xCC). */
  const uint8_t *raw = (const uint8_t *)&spaces[N];
  CHECK(raw[0] == 0xCC, "aspace_partition_no_overrun");

  printf("TEST:PASS:aspace_carve_partition_layout\n");
}

static void test_arena_too_small(void) {
  /* Force per-window size below aspace_window_min_bytes(). Asking for
   * many windows out of a small arena triggers the floor check. */
  address_space_t spaces[4];
  aspace_result_t r = aspace_partition((uintptr_t)g_arena,
                                       /* arena_size = */ 256u,
                                       spaces,
                                       /* count = */ 4u);
  CHECK(r == ASPACE_ERR_ARENA_TOO_SMALL,
        "aspace_carve_arena_too_small_rejects");
  printf("TEST:PASS:aspace_carve_arena_too_small\n");
}

static void test_invalid_arg(void) {
  address_space_t spaces[2];

  aspace_result_t r1 = aspace_partition((uintptr_t)g_arena,
                                        sizeof(g_arena),
                                        NULL,
                                        2u);
  CHECK(r1 == ASPACE_ERR_INVALID_ARG, "aspace_carve_null_out_rejects");

  aspace_result_t r2 = aspace_partition((uintptr_t)g_arena,
                                        sizeof(g_arena),
                                        spaces,
                                        0u);
  CHECK(r2 == ASPACE_ERR_INVALID_ARG, "aspace_carve_zero_count_rejects");

  printf("TEST:PASS:aspace_carve_invalid_arg\n");
}

static void test_min_window_bytes(void) {
  /* Sanity: minimum window must accommodate both stack and scratch. */
  size_t min = aspace_window_min_bytes();
  CHECK(min >= (size_t)PROC_KSTACK_BYTES + (size_t)IPC_MSG_PAYLOAD_MAX,
        "aspace_carve_min_covers_stack_and_scratch");
  printf("TEST:PASS:aspace_carve_min_window_bytes\n");
}

int main(void) {
  test_min_window_bytes();
  test_partition_layout_invariants();
  test_arena_too_small();
  test_invalid_arg();

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:aspace_carve\n");
    return EXIT_FAILURE;
  }
  printf("TEST:PASS:aspace_carve\n");
  return EXIT_SUCCESS;
}
