/**
 * @file address_space.c
 * @brief M1 flat-with-bounds address-space partitioning (issue #248).
 *
 * Purpose:
 *   Implements `aspace_partition`, the pure deterministic helper that
 *   carves a kernel-reserved arena into N equal-sized address-space
 *   windows. Each window's high `PROC_KSTACK_BYTES` are the kernel
 *   stack and its first `IPC_MSG_PAYLOAD_MAX` bytes back the per-
 *   process IPC envelope scratch.
 *
 *   No global state, no allocation. Callers (the cooperative
 *   scheduler in plan #198 slice 3 will be the first) supply the
 *   arena bounds and the output array. The function refuses to
 *   produce a partition where a window would be smaller than the
 *   stack + scratch fixed cost.
 *
 *   Per-window size is the arena size divided by the window count,
 *   floored to a 16-byte multiple so `stack_top` and `base` keep
 *   16-byte alignment regardless of the (already page-aligned) arena
 *   base.
 *
 * Interactions:
 *   - address_space.h: public interface.
 *   - kernel/ipc/ipc_msg.h: source of `IPC_MSG_PAYLOAD_MAX`.
 *
 * Launched by:
 *   Not a standalone process. Linked into the kernel image and into
 *   the host-side `aspace_carve` unit-test binary.
 *
 * Issue: #248.
 */

#include "address_space.h"

#include <stddef.h>
#include <stdint.h>

#include "../ipc/ipc_msg.h"

/*
 * Per-window minimum: kernel stack + at least IPC_MSG_PAYLOAD_MAX of
 * scratch (the scratch lives at the low end of the window, the stack
 * at the high end). A 16-byte safety margin keeps stack_top alignable.
 */
#define ASPACE_ALIGN 16u

static size_t aspace_window_align_down(size_t value) {
  return value & ~(size_t)(ASPACE_ALIGN - 1u);
}

size_t aspace_window_min_bytes(void) {
  /* Both regions plus one alignment slot of padding. */
  size_t need = (size_t)PROC_KSTACK_BYTES + (size_t)IPC_MSG_PAYLOAD_MAX
                + (size_t)ASPACE_ALIGN;
  return aspace_window_align_down(need + (ASPACE_ALIGN - 1u));
}

aspace_result_t aspace_partition(uintptr_t arena_base,
                                 size_t arena_size,
                                 address_space_t *out,
                                 size_t count) {
  if (out == NULL || count == 0u) {
    return ASPACE_ERR_INVALID_ARG;
  }

  /* Floor per-window size to ASPACE_ALIGN so successive `base` values
   * stay aligned (assuming arena_base is itself aligned, which the
   * linker script guarantees via ALIGN(4K)). */
  size_t per = arena_size / count;
  per = aspace_window_align_down(per);

  if (per < aspace_window_min_bytes()) {
    return ASPACE_ERR_ARENA_TOO_SMALL;
  }

  for (size_t i = 0; i < count; ++i) {
    uintptr_t base = arena_base + (uintptr_t)(per * i);
    out[i].base        = base;
    out[i].size        = per;
    out[i].stack_top   = base + (uintptr_t)per;
    out[i].ipc_scratch = (uint8_t *)base;
    out[i].pt_reserved = NULL;
  }

  return ASPACE_OK;
}
