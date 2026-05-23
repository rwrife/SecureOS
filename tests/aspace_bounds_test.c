/**
 * @file aspace_bounds_test.c
 * @brief Host-side unit tests for the M1 `aspace_contains` runtime
 *        bounds-check helper (issue #260, plan
 *        plans/2026-05-20-m1-process-address-space.md slice 2).
 *
 * Scope of this binary (matches the “Done when” items in issue #260
 * that don’t require touching the frozen IPC error/marker grammar):
 *
 *   - Allow case: in-window pointer + length passes
 *     (TEST:PASS:aspace_bounds_allow).
 *   - Deny case: one byte past `base + size` is rejected
 *     (TEST:PASS:aspace_bounds_deny).
 *   - Deny case: a range straddling the upper or lower boundary is
 *     rejected (TEST:PASS:aspace_bounds_straddle).
 *   - Overflow-safety: synthetic windows where `ptr + len` or
 *     `base + size` would wrap past `UINTPTR_MAX` are rejected without
 *     UB (TEST:PASS:aspace_bounds_overflow).
 *   - NULL aspace returns false (TEST:PASS:aspace_bounds_null_aspace).
 *
 * Out of scope (per #260 follow-up — needs a maintainer design call,
 * see the issue thread): wiring the helper into `ipc_send` / `ipc_recv`
 * and the scheduler block/wake paths. The frozen `ipc_result_t` enum
 * (docs/abi/ipc-wire.md §6) and the canonical CAP:DENY grammar
 * (docs/abi/capability-deny-contract.md §4) both forbid the literal
 * `IPC_ERR_BOUNDS` value and `CAP:DENY:<aspace>:ipc_send:bounds`
 * marker the issue body suggests without an ABI / grammar change.
 *
 * Launched by:
 *   build/scripts/test_aspace_bounds.sh (dispatched via
 *   build/scripts/test.sh aspace_bounds).
 *
 * Issue: #260.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/proc/address_space.h"
#include "../kernel/ipc/ipc_msg.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* A small page-aligned-ish backing arena lives in BSS; the helper
 * only ever does pointer arithmetic on the descriptor fields, it
 * never dereferences `ipc_scratch`, so a host buffer is fine. */
static uint8_t g_arena[1u * 1024u * 1024u];

static const address_space_t *partition_one_window(address_space_t *spaces, size_t count) {
  aspace_result_t r = aspace_partition((uintptr_t)g_arena,
                                       sizeof(g_arena),
                                       spaces,
                                       count);
  CHECK(r == ASPACE_OK, "aspace_bounds_setup_partition_ok");
  return &spaces[0];
}

static void test_in_window_allow(void) {
  address_space_t spaces[2];
  const address_space_t *as = partition_one_window(spaces, 2u);
  uint8_t *base = (uint8_t *)as->base;

  /* base, one byte: inside. */
  CHECK(aspace_contains(as, base, 1u), "aspace_bounds_base_onebyte");
  /* full window: inside. */
  CHECK(aspace_contains(as, base, as->size), "aspace_bounds_full_window");
  /* mid-window range: inside. */
  CHECK(aspace_contains(as, base + 16u, 32u), "aspace_bounds_midwindow");
  /* last byte: inside. */
  CHECK(aspace_contains(as, base + as->size - 1u, 1u),
        "aspace_bounds_lastbyte");
  /* zero-len at base: inside. */
  CHECK(aspace_contains(as, base, 0u), "aspace_bounds_base_zerolen");
  /* IPC scratch range: inside (this is the M1 contract — the per-
   * process scratch buffer is always carved inside its own window). */
  CHECK(aspace_contains(as, as->ipc_scratch, IPC_MSG_PAYLOAD_MAX),
        "aspace_bounds_ipc_scratch_in_window");

  printf("TEST:PASS:aspace_bounds_allow\n");
}

static void test_out_of_window_deny(void) {
  address_space_t spaces[2];
  const address_space_t *as = partition_one_window(spaces, 2u);
  uint8_t *base = (uint8_t *)as->base;

  /* One byte past the exclusive end: OUT. */
  CHECK(!aspace_contains(as, base + as->size, 1u),
        "aspace_bounds_one_past_end");
  /* Zero-len at the exclusive end: OUT (boundary point not contained). */
  CHECK(!aspace_contains(as, base + as->size, 0u),
        "aspace_bounds_zerolen_at_end");
  /* One byte before the window start: OUT. */
  CHECK(!aspace_contains(as, base - 1u, 1u),
        "aspace_bounds_one_before_base");
  /* len == window_size + 1 starting at base: OUT. */
  CHECK(!aspace_contains(as, base, as->size + 1u),
        "aspace_bounds_len_overruns");

  printf("TEST:PASS:aspace_bounds_deny\n");
}

static void test_straddle_deny(void) {
  address_space_t spaces[2];
  const address_space_t *as = partition_one_window(spaces, 2u);
  uint8_t *base = (uint8_t *)as->base;

  /* Upper boundary straddle: starts in-window, ends past it. */
  CHECK(!aspace_contains(as, base + as->size - 4u, 8u),
        "aspace_bounds_straddle_upper");
  /* Lower boundary straddle: starts below base, extends into window. */
  CHECK(!aspace_contains(as, base - 4u, 8u),
        "aspace_bounds_straddle_lower");

  printf("TEST:PASS:aspace_bounds_straddle\n");
}

static void test_null_aspace_deny(void) {
  CHECK(!aspace_contains(NULL, g_arena, 1u),
        "aspace_bounds_null_aspace_rejects");
  CHECK(!aspace_contains(NULL, NULL, 0u),
        "aspace_bounds_null_aspace_zerolen_rejects");
  printf("TEST:PASS:aspace_bounds_null_aspace\n");
}

static void test_overflow_safe(void) {
  /* Synthetic aspace whose `p + len` would wrap past UINTPTR_MAX. */
  address_space_t as = {
      .base = (uintptr_t)1u,
      .size = (size_t)(UINTPTR_MAX - 1u), /* end == UINTPTR_MAX */
      .stack_top = 0,
      .ipc_scratch = NULL,
      .pt_reserved = NULL,
  };
  void *p = (void *)(uintptr_t)(UINTPTR_MAX - 4u);
  CHECK(!aspace_contains(&as, p, (size_t)16u),
        "aspace_bounds_p_plus_len_overflow_rejects");

  /* Window whose own base + size would overflow: not containing
   * anything. We treat the malformed window itself as deny-by-default
   * rather than punishing the caller. */
  address_space_t bad = {
      .base = (uintptr_t)16u,
      .size = (size_t)UINTPTR_MAX, /* base + size wraps */
      .stack_top = 0,
      .ipc_scratch = NULL,
      .pt_reserved = NULL,
  };
  CHECK(!aspace_contains(&bad, (void *)(uintptr_t)32u, 1u),
        "aspace_bounds_window_overflow_rejects");

  printf("TEST:PASS:aspace_bounds_overflow\n");
}

int main(void) {
  test_in_window_allow();
  test_out_of_window_deny();
  test_straddle_deny();
  test_null_aspace_deny();
  test_overflow_safe();

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:aspace_bounds\n");
    return EXIT_FAILURE;
  }
  printf("TEST:PASS:aspace_bounds\n");
  return EXIT_SUCCESS;
}
