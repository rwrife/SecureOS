/*
 * src/stddef.c
 * Drift-anchor helpers for the freestanding <stddef.h> nucleus
 * (M7-TOOLCHAIN-004 slice 9, issue #407).
 *
 * Macros (`NULL`, `offsetof`) and typedefs (`size_t`, `ptrdiff_t`,
 * `wchar_t`, `max_align_t`) are not linker symbols; these tiny
 * helpers exist purely so `symbol_set_pinned` + the host unit test
 * have concrete linker anchors that fold each shipped quantity at
 * *this* TU's compile time. If a future edit to
 * include/clib/stddef.h changes a typedef or `offsetof` definition,
 * the helper here keeps returning the new value, and the test's
 * bit-exact compare against its own host expectations flips. Same
 * shape PR #434 used for `<limits.h>` and PR #431 used for
 * `<stdarg.h>`.
 *
 * Containment: freestanding. No libc, no kernel includes, no syscalls.
 */

#include "../include/clib/stddef.h"

/* Probe struct used by clib_stddef_offsetof_probe. Layout chosen so
 * that the standard C-layout rules pin definite offsets: a `char`
 * forces no special alignment for the first member, then a `long`
 * brings the alignment back to 8 on x86_64 SysV, then another `char`
 * sits immediately after. The exact numeric offsets are asserted by
 * the host unit test against `_Alignof(long)` so the helper stays
 * portable to any future host with a different long alignment. */
struct clib_stddef_probe {
  char  a;   /* offset 0 */
  long  b;   /* offset 8 on x86_64 SysV (aligned to _Alignof(long)) */
  char  c;   /* offset 16 */
};

unsigned long clib_stddef_sizeof(int which) {
  switch (which) {
    case CLIB_STDDEF_SIZE_SIZE_T:     return (unsigned long)sizeof(size_t);
    case CLIB_STDDEF_SIZE_PTRDIFF_T:  return (unsigned long)sizeof(ptrdiff_t);
    case CLIB_STDDEF_SIZE_WCHAR_T:    return (unsigned long)sizeof(wchar_t);
    case CLIB_STDDEF_SIZE_MAX_ALIGN:  return (unsigned long)sizeof(max_align_t);
    case CLIB_STDDEF_ALIGN_MAX_ALIGN: return (unsigned long)_Alignof(max_align_t);
    default:                          return 0;
  }
}

unsigned long clib_stddef_offsetof_probe(int which) {
  switch (which) {
    case CLIB_STDDEF_OFF_FIRST:
      return (unsigned long)offsetof(struct clib_stddef_probe, a);
    case CLIB_STDDEF_OFF_SECOND:
      return (unsigned long)offsetof(struct clib_stddef_probe, b);
    case CLIB_STDDEF_OFF_THIRD:
      return (unsigned long)offsetof(struct clib_stddef_probe, c);
    default:
      return (unsigned long)-1;
  }
}
