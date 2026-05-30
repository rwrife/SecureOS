/*
 * user/libs/clib/src/stdarg.c
 *
 * Freestanding `<stdarg.h>` nucleus translation unit
 * (issue #407 / M7-TOOLCHAIN-004 slice 6, plan
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3).
 *
 * The public surface in `include/clib/stdarg.h` is macros + a typedef
 * that forward directly to the compiler's `__builtin_va_*` intrinsics
 * (see header for the rationale — there is no portable hand-written
 * x86_64 SysV `va_arg`). Pure-header slices in this repo still ship a
 * matching `.c` file for two concrete reasons:
 *
 *   1. `symbol_set_pinned` drift guard — every other clib slice (str/
 *      mem PR #416, ctype PR #417, qsort PR #418, stdlib PR #428,
 *      errno PR #430) takes the address of a shipped library symbol
 *      and refuses to link if the symbol disappears, so the slice's
 *      surface cannot silently shrink. A pure-header slice has nothing
 *      to take the address of unless we ship at least one helper.
 *   2. Link sanity — the test driver compiles + links against a real
 *      `libclib.a` archive member contributed by this slice, so a
 *      build-system mistake (e.g. accidentally dropping the source
 *      from `Makefile.secureos`'s file list when the in-OS toolchain
 *      build wires up) trips the test instead of silently link-elides.
 *
 * Therefore this TU ships ONE helper that exercises the full
 * va_start → va_arg → va_copy → va_end walk under the macros we
 * publish, and is taken by-address from the host unit test:
 *
 *     int clib_stdarg_sum_ints(int n, ...);
 *
 *   - `n`  = number of `int` arguments that follow.
 *   - returns the saturating sum of those `n` ints, clamped to
 *     INT_MIN / INT_MAX (defensive: no signed-overflow UB if a
 *     consumer hands us pathological inputs).
 *
 * This is the same shape glibc / musl ship for their internal
 * variadic test helpers, and it is small enough that the link cost
 * for the in-OS toolchain is one function and zero data.
 *
 * No syscall dependency, no allocator dependency, no globals.
 */

#include "../include/clib/stdarg.h"

#include <limits.h>

int clib_stdarg_sum_ints(int n, ...) {
  va_list ap;
  long    acc = 0; /* widen to long so the saturation logic is well-defined */

  if (n <= 0) {
    return 0;
  }

  va_start(ap, n);
  for (int i = 0; i < n; ++i) {
    int v = va_arg(ap, int);
    acc += (long)v;
    if (acc > (long)INT_MAX) {
      acc = (long)INT_MAX;
    } else if (acc < (long)INT_MIN) {
      acc = (long)INT_MIN;
    }
  }
  va_end(ap);

  return (int)acc;
}

/*
 * Second helper: exercises va_copy specifically. TinyCC's
 * `tcc_warning` path takes a `va_list` and forwards it to two distinct
 * sinks (stderr formatter + the in-memory diagnostic buffer); both
 * walks must independently consume the args without disturbing each
 * other, which is exactly what `va_copy` is for. Ship a helper that
 * sums every argument twice (once through the original, once through
 * a copy) so the host test can prove our macro expansion preserves
 * that contract.
 */
int clib_stdarg_sum_ints_via_copy(int n, ...) {
  va_list ap;
  va_list ap2;
  long    acc1 = 0;
  long    acc2 = 0;

  if (n <= 0) {
    return 0;
  }

  va_start(ap, n);
  va_copy(ap2, ap);

  for (int i = 0; i < n; ++i) {
    int v = va_arg(ap, int);
    acc1 += (long)v;
    if (acc1 > (long)INT_MAX) acc1 = (long)INT_MAX;
    else if (acc1 < (long)INT_MIN) acc1 = (long)INT_MIN;
  }
  for (int i = 0; i < n; ++i) {
    int v = va_arg(ap2, int);
    acc2 += (long)v;
    if (acc2 > (long)INT_MAX) acc2 = (long)INT_MAX;
    else if (acc2 < (long)INT_MIN) acc2 = (long)INT_MIN;
  }

  va_end(ap2);
  va_end(ap);

  /* Returns the sum-of-sums; caller asserts it equals 2 * sum_via_walk. */
  long total = acc1 + acc2;
  if (total > (long)INT_MAX) return INT_MAX;
  if (total < (long)INT_MIN) return INT_MIN;
  return (int)total;
}
