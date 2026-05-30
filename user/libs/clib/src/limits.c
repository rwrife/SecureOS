/*
 * src/limits.c
 * Drift-anchor helpers for the freestanding <limits.h> nucleus
 * (M7-TOOLCHAIN-004 slice 8, issue #407).
 *
 * Limit macros are not linker symbols; these tiny helpers exist purely
 * so `symbol_set_pinned` + the host unit test have a concrete linker
 * anchor that folds each macro at *this* TU's compile time. If a future
 * edit to include/clib/limits.h changes a macro value, the helper here
 * keeps returning the new value, and the test's bit-exact compare
 * against its own host `<limits.h>` flips. Same shape PR #431 used for
 * `clib_stdarg_sum_*`.
 *
 * Containment: freestanding. No libc, no kernel includes, no syscalls.
 */

#include "../include/clib/limits.h"

int clib_limits_char_bit(void) {
  return CHAR_BIT;
}

long clib_limits_check_value(int which) {
  switch (which) {
    case CLIB_LIMITS_SCHAR_MIN: return (long)SCHAR_MIN;
    case CLIB_LIMITS_SCHAR_MAX: return (long)SCHAR_MAX;
    case CLIB_LIMITS_UCHAR_MAX: return (long)UCHAR_MAX;
    case CLIB_LIMITS_SHRT_MIN:  return (long)SHRT_MIN;
    case CLIB_LIMITS_SHRT_MAX:  return (long)SHRT_MAX;
    case CLIB_LIMITS_USHRT_MAX: return (long)USHRT_MAX;
    case CLIB_LIMITS_INT_MIN:   return (long)INT_MIN;
    case CLIB_LIMITS_INT_MAX:   return (long)INT_MAX;
    case CLIB_LIMITS_LONG_MIN:  return LONG_MIN;
    case CLIB_LIMITS_LONG_MAX:  return LONG_MAX;
    default: return 0;
  }
}
