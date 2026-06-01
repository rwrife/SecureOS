/**
 * @file app_native_heap.c
 * @brief Per-app native heap window — implementation extracted from
 *        `launcher_exec.c` so the M7-TOOLCHAIN-001 (#421) `mem_brk`
 *        bridge slot can be link-time exercised end-to-end by the
 *        QEMU round-trip peer (`tests/mem_brk_qemu_test.c`, #495)
 *        without dragging the launcher_exec HAL / crypto / IPC /
 *        session-manager dependency surface into the host test build.
 *
 * Contract is unchanged from the prior static helpers in
 * launcher_exec.c. The launcher's bridge install path now wires
 * `bridge->mem_brk = app_native_mem_brk` (extern) and calls
 * `app_native_heap_reset()` on every fresh top-level bridge wire
 * (the `!nested` arm); nested spawns intentionally inherit the
 * parent break.
 *
 * `app_native_heap_break_for_tests` is the only addition vs. the
 * prior inline helpers — a test-only read-back lets the qemu peer
 * assert the post-reset break is 0 without poking the static.
 */

#include "app_native_heap.h"

#include <stdint.h>

/* BSS pool. 4 MiB matches the prior inline definition and the
 * sizing rationale in launcher_exec.c §"M7-TOOLCHAIN-001 / Sizing".
 * Aligned for the worst-case userland allocator load on the
 * supported targets. */
static uint8_t g_native_heap_pool[APP_NATIVE_HEAP_BYTES]
    __attribute__((aligned(16)));
static size_t g_native_heap_break = 0;

void app_native_heap_reset(void) {
  g_native_heap_break = 0;
}

size_t app_native_heap_break_for_tests(void) {
  return g_native_heap_break;
}

int app_native_mem_brk(int delta, void **out_prev_break) {
  size_t prev;
  size_t new_break;

  if (out_prev_break == 0) {
    return 3; /* OS_STATUS_ERROR — NULL out-pointer guard */
  }

  prev = g_native_heap_break;

  if (delta == 0) {
    *out_prev_break = (void *)&g_native_heap_pool[prev];
    return 0;
  }

  if (delta > 0) {
    size_t udelta = (size_t)delta;
    /* Overflow guard: prev + udelta must stay representable AND
     * inside the pool window. First comparison catches arithmetic
     * wrap before it confuses the bounds check. */
    if (udelta > APP_NATIVE_HEAP_BYTES - prev) {
      *out_prev_break = (void *)&g_native_heap_pool[prev];
      return 1; /* OS_STATUS_DENIED — out-of-arena */
    }
    new_break = prev + udelta;
  } else {
    size_t udelta = (size_t)(-(long)delta);
    if (udelta > prev) {
      /* Cannot shrink below the pool base — deny cleanly. */
      *out_prev_break = (void *)&g_native_heap_pool[prev];
      return 1;
    }
    new_break = prev - udelta;
  }

  g_native_heap_break = new_break;
  /* sbrk(2): on success, return the *previous* break (the address
   * of the first freshly-committed byte on positive delta). */
  *out_prev_break = (void *)&g_native_heap_pool[prev];
  return 0;
}
