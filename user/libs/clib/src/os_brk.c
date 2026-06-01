/**
 * @file src/os_brk.c
 * @brief Implementation of the `clib_brk_fn`-shaped forwarder around
 *        `os_mem_brk` (M7-TOOLCHAIN-001 slice 3, issue #421).
 *
 * See `include/clib/os_brk.h` for the contract.
 */

#include "../include/clib/os_brk.h"

#include <limits.h> /* INT_MAX */
#include <stddef.h>

/*
 * The on-target runtime wrapper lives in `user/runtime/secureos_api_stubs.c`
 * and is declared in `user/include/secureos_api.h`. The SDK build wires
 * `-Iuser/include` for the freestanding clib library (see
 * `build/scripts/build_user_lib.sh`), so the bare include resolves.
 *
 * Pulling this header is the only kernel-adjacent dependency the
 * forwarder takes; everything else in `user/libs/clib` is pure
 * freestanding C. `validate_sdk_no_kernel_includes` continues to
 * pass because `secureos_api.h` is itself a user-include and pulls
 * `secureos_abi.h`, not any `kernel/` header.
 */
#include "secureos_api.h"

void *clib_os_brk(void *ctx, size_t delta) {
  void *prev_break = (void *)0;
  os_status_t st;

  (void)ctx; /* the on-target heap is process-global */

  /*
   * Defensive narrowing guard. `clib_brk_fn` takes `size_t`; the
   * syscall takes a signed `int`. `os_mem_brk(delta < 0)` is the
   * shrink path, which the allocator never wants to trigger from
   * a growth callback — so reject any `delta` that would alias
   * into that branch (or be a zero no-op).
   */
  if (delta == 0u || delta > (size_t)INT_MAX) {
    return (void *)0;
  }

  st = os_mem_brk((int)delta, &prev_break);
  if (st != OS_STATUS_OK) {
    /*
     * `OS_STATUS_DENIED` (out-of-arena) and `OS_STATUS_ERROR`
     * (no bridge / NULL out / wrapper failure) both collapse to
     * the allocator's documented "growth failed, fail the
     * originating malloc cleanly" signal: NULL.
     */
    return (void *)0;
  }

  /*
   * `os_mem_brk` writes the previous break into `*out_prev_break`
   * on success. The `clib_brk_fn` contract says we return the
   * first byte of the freshly committed region — that is exactly
   * the previous break (sbrk-shape semantics; see
   * docs/abi/syscalls.md `os_mem_brk` row).
   */
  return prev_break;
}
