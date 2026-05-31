/**
 * @file src/float.c
 * @brief Freestanding <float.h> drift-anchor helper TU for
 *        user/libs/clib (issue #407 slice, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * `<float.h>` is header-only — the standard mandates a set of
 * compile-time constants (and one rounding-mode expression), so
 * there is no implementation to link. This TU's only job is to fold
 * each macro into a value at THIS TU's compile time so the host
 * unit test can round-trip the result through the LINKED `libclib`
 * and detect any future drift (e.g. a stray redefinition of
 * `DBL_MAX` to a wrong magnitude, or a flipped sign on
 * `FLT_MIN_10_EXP`).
 *
 * No libc dependency, no syscalls. Compiles under `-ffreestanding`.
 */

#include "../include/clib/float.h"

/* Sub-selector enum lets the test ask the helper for each macro's
 * folded value without exporting one symbol per macro. Values are
 * stable; do not reorder. */
enum clib_float_op {
  /* mode / radix integers */
  CLIB_FLOAT_OP_RADIX                = 0,
  CLIB_FLOAT_OP_EVAL_METHOD          = 1,
  CLIB_FLOAT_OP_ROUNDS               = 2,
  CLIB_FLOAT_OP_FLT_HAS_SUBNORM      = 3,
  CLIB_FLOAT_OP_DBL_HAS_SUBNORM      = 4,
  CLIB_FLOAT_OP_LDBL_HAS_SUBNORM     = 5,
  CLIB_FLOAT_OP_DECIMAL_DIG          = 6,
  /* per-type integers (mantissa / decimal / exponent envelopes) */
  CLIB_FLOAT_OP_FLT_MANT_DIG         = 7,
  CLIB_FLOAT_OP_DBL_MANT_DIG         = 8,
  CLIB_FLOAT_OP_LDBL_MANT_DIG        = 9,
  CLIB_FLOAT_OP_FLT_DECIMAL_DIG      = 10,
  CLIB_FLOAT_OP_DBL_DECIMAL_DIG      = 11,
  CLIB_FLOAT_OP_LDBL_DECIMAL_DIG     = 12,
  CLIB_FLOAT_OP_FLT_DIG              = 13,
  CLIB_FLOAT_OP_DBL_DIG              = 14,
  CLIB_FLOAT_OP_LDBL_DIG             = 15,
  CLIB_FLOAT_OP_FLT_MIN_EXP          = 16,
  CLIB_FLOAT_OP_DBL_MIN_EXP          = 17,
  CLIB_FLOAT_OP_LDBL_MIN_EXP         = 18,
  CLIB_FLOAT_OP_FLT_MIN_10_EXP       = 19,
  CLIB_FLOAT_OP_DBL_MIN_10_EXP       = 20,
  CLIB_FLOAT_OP_LDBL_MIN_10_EXP      = 21,
  CLIB_FLOAT_OP_FLT_MAX_EXP          = 22,
  CLIB_FLOAT_OP_DBL_MAX_EXP          = 23,
  CLIB_FLOAT_OP_LDBL_MAX_EXP         = 24,
  CLIB_FLOAT_OP_FLT_MAX_10_EXP       = 25,
  CLIB_FLOAT_OP_DBL_MAX_10_EXP       = 26,
  CLIB_FLOAT_OP_LDBL_MAX_10_EXP      = 27,
  CLIB_FLOAT_OP__INT_COUNT           = 28
};

/* Fold each integer-valued macro at THIS TU's compile time. Returns
 * INT_MIN-equivalent sentinel for unknown selectors so the test can
 * pin the count, too. */
long clib_float_eval_int(int op) {
  switch (op) {
    case CLIB_FLOAT_OP_RADIX:            return (long)FLT_RADIX;
    case CLIB_FLOAT_OP_EVAL_METHOD:      return (long)FLT_EVAL_METHOD;
    case CLIB_FLOAT_OP_ROUNDS:           return (long)FLT_ROUNDS;
    case CLIB_FLOAT_OP_FLT_HAS_SUBNORM:  return (long)FLT_HAS_SUBNORM;
    case CLIB_FLOAT_OP_DBL_HAS_SUBNORM:  return (long)DBL_HAS_SUBNORM;
    case CLIB_FLOAT_OP_LDBL_HAS_SUBNORM: return (long)LDBL_HAS_SUBNORM;
    case CLIB_FLOAT_OP_DECIMAL_DIG:      return (long)DECIMAL_DIG;

    case CLIB_FLOAT_OP_FLT_MANT_DIG:     return (long)FLT_MANT_DIG;
    case CLIB_FLOAT_OP_DBL_MANT_DIG:     return (long)DBL_MANT_DIG;
    case CLIB_FLOAT_OP_LDBL_MANT_DIG:    return (long)LDBL_MANT_DIG;
    case CLIB_FLOAT_OP_FLT_DECIMAL_DIG:  return (long)FLT_DECIMAL_DIG;
    case CLIB_FLOAT_OP_DBL_DECIMAL_DIG:  return (long)DBL_DECIMAL_DIG;
    case CLIB_FLOAT_OP_LDBL_DECIMAL_DIG: return (long)LDBL_DECIMAL_DIG;
    case CLIB_FLOAT_OP_FLT_DIG:          return (long)FLT_DIG;
    case CLIB_FLOAT_OP_DBL_DIG:          return (long)DBL_DIG;
    case CLIB_FLOAT_OP_LDBL_DIG:         return (long)LDBL_DIG;

    case CLIB_FLOAT_OP_FLT_MIN_EXP:      return (long)FLT_MIN_EXP;
    case CLIB_FLOAT_OP_DBL_MIN_EXP:      return (long)DBL_MIN_EXP;
    case CLIB_FLOAT_OP_LDBL_MIN_EXP:     return (long)LDBL_MIN_EXP;
    case CLIB_FLOAT_OP_FLT_MIN_10_EXP:   return (long)FLT_MIN_10_EXP;
    case CLIB_FLOAT_OP_DBL_MIN_10_EXP:   return (long)DBL_MIN_10_EXP;
    case CLIB_FLOAT_OP_LDBL_MIN_10_EXP:  return (long)LDBL_MIN_10_EXP;
    case CLIB_FLOAT_OP_FLT_MAX_EXP:      return (long)FLT_MAX_EXP;
    case CLIB_FLOAT_OP_DBL_MAX_EXP:      return (long)DBL_MAX_EXP;
    case CLIB_FLOAT_OP_LDBL_MAX_EXP:     return (long)LDBL_MAX_EXP;
    case CLIB_FLOAT_OP_FLT_MAX_10_EXP:   return (long)FLT_MAX_10_EXP;
    case CLIB_FLOAT_OP_DBL_MAX_10_EXP:   return (long)DBL_MAX_10_EXP;
    case CLIB_FLOAT_OP_LDBL_MAX_10_EXP:  return (long)LDBL_MAX_10_EXP;

    default:                             return -2147483647L - 1L;
  }
}

int clib_float_int_op_count(void) {
  return (int)CLIB_FLOAT_OP__INT_COUNT;
}

/* Sub-selector enum for the per-type FP-valued macros. Kept separate
 * from the int selectors so the helper's return type is unambiguous. */
enum clib_float_fp_op {
  CLIB_FLOAT_FP_OP_FLT_MAX           = 0,
  CLIB_FLOAT_FP_OP_DBL_MAX           = 1,
  CLIB_FLOAT_FP_OP_LDBL_MAX          = 2,
  CLIB_FLOAT_FP_OP_FLT_MIN           = 3,
  CLIB_FLOAT_FP_OP_DBL_MIN           = 4,
  CLIB_FLOAT_FP_OP_LDBL_MIN          = 5,
  CLIB_FLOAT_FP_OP_FLT_EPSILON       = 6,
  CLIB_FLOAT_FP_OP_DBL_EPSILON       = 7,
  CLIB_FLOAT_FP_OP_LDBL_EPSILON      = 8,
  CLIB_FLOAT_FP_OP_FLT_TRUE_MIN      = 9,
  CLIB_FLOAT_FP_OP_DBL_TRUE_MIN      = 10,
  CLIB_FLOAT_FP_OP_LDBL_TRUE_MIN     = 11,
  CLIB_FLOAT_FP_OP__COUNT            = 12
};

/* Returns the folded value as `long double` so the test can compare
 * each per-type bound (cast back) without losing precision on the
 * long-double cases. Unknown selectors return 0.0L. */
long double clib_float_eval_fp(int op) {
  switch (op) {
    case CLIB_FLOAT_FP_OP_FLT_MAX:        return (long double)FLT_MAX;
    case CLIB_FLOAT_FP_OP_DBL_MAX:        return (long double)DBL_MAX;
    case CLIB_FLOAT_FP_OP_LDBL_MAX:       return (long double)LDBL_MAX;
    case CLIB_FLOAT_FP_OP_FLT_MIN:        return (long double)FLT_MIN;
    case CLIB_FLOAT_FP_OP_DBL_MIN:        return (long double)DBL_MIN;
    case CLIB_FLOAT_FP_OP_LDBL_MIN:       return (long double)LDBL_MIN;
    case CLIB_FLOAT_FP_OP_FLT_EPSILON:    return (long double)FLT_EPSILON;
    case CLIB_FLOAT_FP_OP_DBL_EPSILON:    return (long double)DBL_EPSILON;
    case CLIB_FLOAT_FP_OP_LDBL_EPSILON:   return (long double)LDBL_EPSILON;
#ifdef FLT_TRUE_MIN
    case CLIB_FLOAT_FP_OP_FLT_TRUE_MIN:   return (long double)FLT_TRUE_MIN;
#endif
#ifdef DBL_TRUE_MIN
    case CLIB_FLOAT_FP_OP_DBL_TRUE_MIN:   return (long double)DBL_TRUE_MIN;
#endif
#ifdef LDBL_TRUE_MIN
    case CLIB_FLOAT_FP_OP_LDBL_TRUE_MIN:  return (long double)LDBL_TRUE_MIN;
#endif
    default:                              return 0.0L;
  }
}

int clib_float_fp_op_count(void) {
  return (int)CLIB_FLOAT_FP_OP__COUNT;
}
