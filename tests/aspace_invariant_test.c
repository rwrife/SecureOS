/**
 * @file aspace_invariant_test.c
 * @brief Host-side unit tests for the M1 `aspace_invariant_ok()`
 *        layout-invariant predicate (issue #260, scheduler block/wake
 *        half — the kernel-internal panic site that the future
 *        `proc_sched_*` wiring will call before restoring a PCB
 *        context).
 *
 * Scope (kernel-internal only — no ABI surface, no IPC error code,
 * no CAP:DENY marker; those remain blocked on the maintainer design
 * call discussed in #260 / #261 thread):
 *
 *   - Allow: a freshly partitioned aspace (the only way the scheduler
 *     ever obtains a window in M1) passes the invariant
 *     (TEST:PASS:aspace_invariant_allow_partitioned).
 *   - Allow: a hand-built window with NULL ipc_scratch passes (the
 *     boot idle PCB legitimately carries no scratch region)
 *     (TEST:PASS:aspace_invariant_allow_no_scratch).
 *   - Deny: NULL aspace, zero-size window, base+size overflow,
 *     stack_top below base, stack_top past window end
 *     (TEST:PASS:aspace_invariant_deny_layout).
 *   - Deny: non-NULL ipc_scratch that does not fit the full
 *     IPC_MSG_PAYLOAD_MAX span inside the window
 *     (TEST:PASS:aspace_invariant_deny_scratch_escapes).
 *
 * Why this test exists separately from aspace_bounds_test:
 *   `aspace_contains()` is a runtime pointer/length check the IPC
 *   layer will eventually call on every send/recv. `aspace_invariant_ok()`
 *   is a layout audit the scheduler will eventually call exactly
 *   once per dispatch. Their failure modes are different (per-call
 *   deny vs. fatal panic), so the tests are kept in separate
 *   binaries so a regression in one cannot mask the other.
 *
 * Launched by:
 *   build/scripts/test_aspace_invariant.sh (dispatched via
 *   build/scripts/test.sh aspace_invariant).
 *
 * Issue: #260.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../kernel/proc/address_space.h"
#include "../kernel/ipc/ipc_msg.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* Backing arena lives in BSS. The invariant helper never dereferences
 * any of the descriptor pointers, it only inspects the numeric
 * fields, so a plain host buffer is sufficient. */
static uint8_t g_arena[1u * 1024u * 1024u];

static void test_allow_partitioned(void) {
  address_space_t spaces[4];
  aspace_result_t r = aspace_partition((uintptr_t)g_arena,
                                       sizeof(g_arena),
                                       spaces,
                                       4u);
  CHECK(r == ASPACE_OK, "aspace_invariant_setup_partition_ok");

  /* Every window produced by aspace_partition() must satisfy the
   * invariant by construction — this is the contract the scheduler
   * relies on. */
  for (size_t i = 0; i < 4u; ++i) {
    CHECK(aspace_invariant_ok(&spaces[i]),
          "aspace_invariant_partitioned_window_ok");
  }

  printf("TEST:PASS:aspace_invariant_allow_partitioned\n");
}

static void test_allow_no_scratch(void) {
  /* Hand-built window mirroring the M1 boot idle PCB layout: a
   * kernel-reserved arena with a usable stack but no per-process IPC
   * scratch region. The invariant must accept this. */
  address_space_t as = {
      .base = (uintptr_t)g_arena,
      .size = (size_t)4096u,
      .stack_top = (uintptr_t)g_arena + (uintptr_t)4096u,
      .ipc_scratch = NULL,
      .pt_reserved = NULL,
  };
  CHECK(aspace_invariant_ok(&as), "aspace_invariant_no_scratch_ok");

  /* stack_top strictly above base but not at end is also fine. */
  as.stack_top = (uintptr_t)g_arena + (uintptr_t)2048u;
  CHECK(aspace_invariant_ok(&as), "aspace_invariant_stack_below_end_ok");

  printf("TEST:PASS:aspace_invariant_allow_no_scratch\n");
}

static void test_deny_layout(void) {
  /* NULL. */
  CHECK(!aspace_invariant_ok(NULL), "aspace_invariant_null_deny");

  /* Zero-size window. */
  address_space_t zero = {
      .base = (uintptr_t)g_arena,
      .size = 0u,
      .stack_top = (uintptr_t)g_arena,
      .ipc_scratch = NULL,
      .pt_reserved = NULL,
  };
  CHECK(!aspace_invariant_ok(&zero), "aspace_invariant_zero_size_deny");

  /* base + size would wrap past UINTPTR_MAX. */
  address_space_t wrap = {
      .base = (uintptr_t)16u,
      .size = (size_t)UINTPTR_MAX,
      .stack_top = (uintptr_t)32u,
      .ipc_scratch = NULL,
      .pt_reserved = NULL,
  };
  CHECK(!aspace_invariant_ok(&wrap), "aspace_invariant_wrap_deny");

  /* stack_top below base — corrupted layout, scheduler must refuse. */
  address_space_t low = {
      .base = (uintptr_t)g_arena + 1024u,
      .size = 1024u,
      .stack_top = (uintptr_t)g_arena, /* below base */
      .ipc_scratch = NULL,
      .pt_reserved = NULL,
  };
  CHECK(!aspace_invariant_ok(&low), "aspace_invariant_stack_below_base_deny");

  /* stack_top equal to base — zero-size stack, scheduler would
   * immediately underflow on first push. Treat as invariant break. */
  address_space_t eq = {
      .base = (uintptr_t)g_arena,
      .size = 1024u,
      .stack_top = (uintptr_t)g_arena, /* equals base */
      .ipc_scratch = NULL,
      .pt_reserved = NULL,
  };
  CHECK(!aspace_invariant_ok(&eq), "aspace_invariant_stack_at_base_deny");

  /* stack_top one byte past the exclusive window end. */
  address_space_t high = {
      .base = (uintptr_t)g_arena,
      .size = 1024u,
      .stack_top = (uintptr_t)g_arena + 1025u,
      .ipc_scratch = NULL,
      .pt_reserved = NULL,
  };
  CHECK(!aspace_invariant_ok(&high), "aspace_invariant_stack_past_end_deny");

  printf("TEST:PASS:aspace_invariant_deny_layout\n");
}

static void test_deny_scratch_escapes(void) {
  /* Window large enough to hold a stack but whose ipc_scratch is
   * pointed outside the window. The scheduler must refuse such a
   * PCB rather than silently restore it. */
  address_space_t outside = {
      .base = (uintptr_t)g_arena,
      .size = (size_t)(PROC_KSTACK_BYTES + IPC_MSG_PAYLOAD_MAX + 64u),
      .stack_top = (uintptr_t)g_arena
                   + (uintptr_t)(PROC_KSTACK_BYTES + IPC_MSG_PAYLOAD_MAX + 64u),
      .ipc_scratch = (uint8_t *)((uintptr_t)g_arena + (uintptr_t)sizeof(g_arena)),
      .pt_reserved = NULL,
  };
  CHECK(!aspace_invariant_ok(&outside),
        "aspace_invariant_scratch_outside_deny");

  /* ipc_scratch starts in-window but its IPC_MSG_PAYLOAD_MAX span
   * runs past the window end. */
  address_space_t straddle = {
      .base = (uintptr_t)g_arena,
      .size = (size_t)IPC_MSG_PAYLOAD_MAX, /* exactly one scratch worth */
      .stack_top = (uintptr_t)g_arena + (uintptr_t)IPC_MSG_PAYLOAD_MAX,
      /* scratch starts one byte in, so its tail is one byte past end. */
      .ipc_scratch = (uint8_t *)((uintptr_t)g_arena + 1u),
      .pt_reserved = NULL,
  };
  CHECK(!aspace_invariant_ok(&straddle),
        "aspace_invariant_scratch_straddle_deny");

  printf("TEST:PASS:aspace_invariant_deny_scratch_escapes\n");
}

int main(void) {
  test_allow_partitioned();
  test_allow_no_scratch();
  test_deny_layout();
  test_deny_scratch_escapes();

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:aspace_invariant\n");
    return EXIT_FAILURE;
  }
  printf("TEST:PASS:aspace_invariant\n");
  return EXIT_SUCCESS;
}
