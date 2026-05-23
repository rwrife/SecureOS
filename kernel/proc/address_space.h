/**
 * @file address_space.h
 * @brief M1 flat-with-bounds address_space_t + arena partitioning helper
 *        (issue #248, plan plans/2026-05-20-m1-process-address-space.md
 *        slice 2).
 *
 * Purpose:
 *   Lands the concrete `address_space_t` shape that the M1 plan reserved
 *   as an opaque pointer in #224. Each address space describes a single
 *   contiguous `[base, base+size)` window carved out of a kernel-reserved
 *   `.proc_arena` BSS region (see kernel/arch/x86/boot/linker.ld). The
 *   high `PROC_KSTACK_BYTES` of each window are the per-process kernel
 *   stack (top = `stack_top`); the low `IPC_MSG_PAYLOAD_MAX` bytes back
 *   the per-process IPC envelope scratch buffer (`ipc_scratch`).
 *
 *   `aspace_partition` is deterministic and pure: given an arena
 *   `[base, base+size)` and a window count, it produces `count`
 *   non-overlapping, equal-sized address spaces in caller-provided
 *   storage. No global state, no allocation. The cooperative scheduler
 *   slice (plan #198 slice 3) is the first caller that will wire this
 *   to `__proc_arena_start` / `__proc_arena_end` from boot.
 *
 *   What this slice DOES NOT do (explicit non-asks):
 *     - No MMU enforcement, no page-table construction. `pt_reserved`
 *       is the forward-compat hook for the M2 paging slice and is
 *       always initialised to NULL here.
 *     - No coupling to `process_t`. `process_create` continues to
 *       store whatever `address_space_t *` the caller passes; wiring
 *       `proc_init` to consume partitioned slots lands with slice 3.
 *     - No syscall or IPC surface change. `IPC_MSG_PAYLOAD_MAX` is
 *       consumed by reference only.
 *
 * Interactions:
 *   - kernel/ipc/ipc_msg.h: source of `IPC_MSG_PAYLOAD_MAX` (#220).
 *   - kernel/proc/process.h: keeps `address_space_t` as an opaque
 *     pointer; this header completes the struct definition.
 *   - kernel/arch/x86/boot/linker.ld: defines the `.proc_arena`
 *     section + `__proc_arena_start` / `__proc_arena_end` anchors.
 *
 * Launched by:
 *   Not a standalone process. Header consumed by the kernel image and
 *   the host-side `aspace_carve` unit-test binary.
 *
 * Issue: #248. Plan: plans/2026-05-20-m1-process-address-space.md.
 */

#ifndef SECUREOS_KERNEL_PROC_ADDRESS_SPACE_H
#define SECUREOS_KERNEL_PROC_ADDRESS_SPACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Per-process kernel-stack size. Matches the value called out in the
 * plan ("fixed 16 KiB"). Kept as a kernel-internal constant — not part
 * of the user/kernel ABI surface, so adjusting it does NOT require an
 * OS_ABI_VERSION bump.
 */
#define PROC_KSTACK_BYTES (16u * 1024u)

/*
 * Total bytes reserved for the process arena in the kernel image.
 * The linker script (kernel/arch/x86/boot/linker.ld) carves out a
 * `.proc_arena` BSS region of exactly this size, page-aligned, and
 * exposes `__proc_arena_start` / `__proc_arena_end` for the scheduler
 * slice to feed into `aspace_partition`.
 */
#define PROC_ARENA_BYTES (1u * 1024u * 1024u)

/*
 * Concrete address-space shape. Field order is part of the v0
 * contract — tests pin sizeof/offsetof in aspace_carve_test.c. New
 * fields go at the end.
 */
typedef struct address_space {
  uintptr_t  base;         /* low end of the bounds window (inclusive) */
  size_t     size;         /* total bytes in the window */
  uintptr_t  stack_top;    /* top of the per-process kernel stack */
  uint8_t   *ipc_scratch;  /* IPC_MSG_PAYLOAD_MAX bytes, inside [base,base+size) */
  void      *pt_reserved;  /* page-table handle slot, reserved for M2+ */
} address_space_t;

/*
 * Result vocabulary. Distinct enum keeps callers from accidentally
 * merging an aspace error into a proc/ipc/cap switch.
 */
typedef enum {
  ASPACE_OK = 0,
  ASPACE_ERR_INVALID_ARG = 1,
  ASPACE_ERR_ARENA_TOO_SMALL = 2,
} aspace_result_t;

/*
 * Minimum bytes per window. The kernel stack and IPC scratch must
 * both fit, and the layout requires `stack_top` to live inside the
 * window. Exposed for tests and the partitioning helper.
 */
size_t aspace_window_min_bytes(void);

/*
 * Deterministically partition `[arena_base, arena_base + arena_size)`
 * into `count` non-overlapping, equal-sized windows and populate
 * `out[0 .. count-1]`. All fields are initialised:
 *   - base       : arena_base + i * per_window_size
 *   - size       : per_window_size (== arena_size / count, rounded down
 *                  to a multiple of 16 so stack_top stays aligned)
 *   - stack_top  : base + size  (top-of-stack, grows downward)
 *   - ipc_scratch: (uint8_t *) base
 *   - pt_reserved: NULL
 *
 * Returns:
 *   ASPACE_OK                 - all `count` windows initialised.
 *   ASPACE_ERR_INVALID_ARG    - out == NULL || count == 0.
 *   ASPACE_ERR_ARENA_TOO_SMALL- per_window_size < aspace_window_min_bytes().
 *
 * On any error the contents of `out` are unspecified.
 */
aspace_result_t aspace_partition(uintptr_t arena_base,
                                 size_t arena_size,
                                 address_space_t *out,
                                 size_t count);

/*
 * Bounds predicate for the M1 flat-with-bounds enforcement check
 * (issue #260). Returns `true` iff the entire byte range
 * `[ptr, ptr + len)` is contained inside `[as->base, as->base + as->size)`.
 *
 * Contract:
 *   - `as == NULL` returns `false` (no window, nothing is contained).
 *   - `len == 0` returns `true` iff `ptr` itself is in the half-open
 *     window. The empty range at the boundary `ptr == base + size` is
 *     rejected; an empty range exactly at `base` is accepted.
 *   - Pointer arithmetic on `ptr + len` is performed in `uintptr_t`
 *     with overflow rejection: any `(uintptr_t)ptr + len` that wraps
 *     past `UINTPTR_MAX` returns `false`. Likewise any window whose
 *     own `base + size` would overflow is treated as not containing
 *     anything (rejects the malformed window rather than the caller).
 *
 * This is the M1 substitute for page-table enforcement; the plan
 * (`plans/2026-05-20-m1-process-address-space.md` slice 2) commits
 * the kernel to consulting this predicate before trusting any
 * user-supplied pointer that names a region inside an
 * `address_space_t` window.
 */
bool aspace_contains(const address_space_t *as,
                     const void *ptr,
                     size_t len);

/*
 * Kernel-internal layout-invariant predicate (issue #260, scheduler
 * block/wake half).
 *
 * Returns `true` iff `as` describes a self-consistent window:
 *   - `as != NULL`.
 *   - `as->size > 0` and `as->base + as->size` does not overflow.
 *   - `as->stack_top` lies strictly above `as->base` and at most at
 *     the exclusive upper bound `as->base + as->size` — i.e. a
 *     non-empty stack region that does not escape the window.
 *   - `as->ipc_scratch == NULL` is permitted (an aspace may carry no
 *     scratch region), but if non-NULL, the full `IPC_MSG_PAYLOAD_MAX`
 *     scratch span must lie inside `[base, base + size)`.
 *
 * This is the M1 scheduler's substitute for a hardware page-table
 * invariant check: every PCB whose context is about to be restored
 * must satisfy `aspace_invariant_ok(pcb->aspace)`, otherwise the
 * scheduler has been handed a corrupted window and continuing would
 * silently restore an out-of-bounds stack pointer. The scheduler
 * panics on a `false` return (issue #260 done-when 3); this predicate
 * exists so the panic site is byte-identical to the host-side
 * regression test asserting the same invariant.
 *
 * Pure function: no I/O, no globals, no allocation. Safe to call
 * from any context.
 */
bool aspace_invariant_ok(const address_space_t *as);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_PROC_ADDRESS_SPACE_H */
