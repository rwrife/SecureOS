/**
 * @file clib_float_test.c
 * @brief Host unit test for the freestanding <float.h> nucleus
 *        (issue #407 slice, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Covers:
 *   1. All C11-mandated macros are #defined by OUR header.
 *   2. C11 §5.2.4.2.2¶8/¶10/¶11 minima are satisfied:
 *        FLT_RADIX >= 2, *_MANT_DIG >= 1,
 *        FLT_DIG  >= 6 / DBL_DIG  >= 10 / LDBL_DIG >= 10,
 *        *_MIN_10_EXP <= -37, *_MAX_10_EXP >= +37,
 *        FLT_EPSILON  <= 1e-5,  DBL_EPSILON  <= 1e-9,
 *        LDBL_EPSILON <= 1e-9,
 *        FLT_MAX   >= 1e37,  DBL_MAX   >= 1e37, LDBL_MAX >= 1e37,
 *        FLT_MIN   <= 1e-37, DBL_MIN   <= 1e-37, LDBL_MIN <= 1e-37,
 *        DECIMAL_DIG >= 10.
 *      `FLT_ROUNDS` must evaluate to an int in -1..3 (§5.2.4.2.2¶8).
 *      `FLT_EVAL_METHOD` must evaluate to an int (any value).
 *      `*_HAS_SUBNORM` must evaluate to one of -1, 0, 1 (§5.2.4.2.2¶10).
 *   3. Each macro round-trips through the LINKED helper TU
 *      (`src/float.c`): the value this TU folds via OUR header equals
 *      the value the helper TU folded at its own compile time.
 *      Without the helper-TU round-trip a drift that affected only
 *      one TU could escape.
 *   4. The helper TU exports `clib_float_eval_int`,
 *      `clib_float_int_op_count`, `clib_float_eval_fp`,
 *      `clib_float_fp_op_count` (`symbol_set_pinned`).
 *
 * Compiled with `-fno-builtin`. We deliberately do NOT include the
 * host `<float.h>` so the header-required minima are read straight
 * from OUR macros (a missing macro would fail to compile).
 *
 * Launched by:
 *   build/scripts/test_clib_float.sh (dispatched via
 *   build/scripts/test.sh clib_float).
 */

#include <stdio.h>

#include "../user/libs/clib/include/clib/float.h"

/* Helper-TU selectors — kept in sync by hand with src/float.c. The
 * link step would fail loudly if either prototype drifts. */
enum {
  /* int selectors */
  CLIB_FLOAT_OP_RADIX                = 0,
  CLIB_FLOAT_OP_EVAL_METHOD          = 1,
  CLIB_FLOAT_OP_ROUNDS               = 2,
  CLIB_FLOAT_OP_FLT_HAS_SUBNORM      = 3,
  CLIB_FLOAT_OP_DBL_HAS_SUBNORM      = 4,
  CLIB_FLOAT_OP_LDBL_HAS_SUBNORM     = 5,
  CLIB_FLOAT_OP_DECIMAL_DIG          = 6,
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
  CLIB_FLOAT_OP__INT_COUNT           = 28,

  /* fp selectors */
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

long        clib_float_eval_int(int op);
int         clib_float_int_op_count(void);
long double clib_float_eval_fp(int op);
int         clib_float_fp_op_count(void);

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:clib_float:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* ---- 1. all C11-mandated macros are defined --------------------------- */
static void check_macros_defined(void) {
#ifndef FLT_RADIX
  CHECK(0, "FLT_RADIX_not_defined");
#endif
#ifndef FLT_ROUNDS
  CHECK(0, "FLT_ROUNDS_not_defined");
#endif
#ifndef FLT_EVAL_METHOD
  CHECK(0, "FLT_EVAL_METHOD_not_defined");
#endif
#ifndef FLT_HAS_SUBNORM
  CHECK(0, "FLT_HAS_SUBNORM_not_defined");
#endif
#ifndef DBL_HAS_SUBNORM
  CHECK(0, "DBL_HAS_SUBNORM_not_defined");
#endif
#ifndef LDBL_HAS_SUBNORM
  CHECK(0, "LDBL_HAS_SUBNORM_not_defined");
#endif
#ifndef DECIMAL_DIG
  CHECK(0, "DECIMAL_DIG_not_defined");
#endif
#if !defined(FLT_MANT_DIG) || !defined(DBL_MANT_DIG) || !defined(LDBL_MANT_DIG)
  CHECK(0, "MANT_DIG_not_defined");
#endif
#if !defined(FLT_DECIMAL_DIG) || !defined(DBL_DECIMAL_DIG) || !defined(LDBL_DECIMAL_DIG)
  CHECK(0, "DECIMAL_DIG_per_type_not_defined");
#endif
#if !defined(FLT_DIG) || !defined(DBL_DIG) || !defined(LDBL_DIG)
  CHECK(0, "DIG_not_defined");
#endif
#if !defined(FLT_MIN_EXP) || !defined(DBL_MIN_EXP) || !defined(LDBL_MIN_EXP)
  CHECK(0, "MIN_EXP_not_defined");
#endif
#if !defined(FLT_MIN_10_EXP) || !defined(DBL_MIN_10_EXP) || !defined(LDBL_MIN_10_EXP)
  CHECK(0, "MIN_10_EXP_not_defined");
#endif
#if !defined(FLT_MAX_EXP) || !defined(DBL_MAX_EXP) || !defined(LDBL_MAX_EXP)
  CHECK(0, "MAX_EXP_not_defined");
#endif
#if !defined(FLT_MAX_10_EXP) || !defined(DBL_MAX_10_EXP) || !defined(LDBL_MAX_10_EXP)
  CHECK(0, "MAX_10_EXP_not_defined");
#endif
#if !defined(FLT_MAX) || !defined(DBL_MAX) || !defined(LDBL_MAX)
  CHECK(0, "MAX_not_defined");
#endif
#if !defined(FLT_EPSILON) || !defined(DBL_EPSILON) || !defined(LDBL_EPSILON)
  CHECK(0, "EPSILON_not_defined");
#endif
#if !defined(FLT_MIN) || !defined(DBL_MIN) || !defined(LDBL_MIN)
  CHECK(0, "MIN_not_defined");
#endif
  (void)g_fail;
}

/* ---- 2. C11 minima -------------------------------------------------- */
static void check_c11_minima(void) {
  /* §5.2.4.2.2¶8 */
  CHECK(FLT_RADIX  >= 2,         "FLT_RADIX_min");
  CHECK(FLT_MANT_DIG  >= 1,      "FLT_MANT_DIG_min");
  CHECK(DBL_MANT_DIG  >= 1,      "DBL_MANT_DIG_min");
  CHECK(LDBL_MANT_DIG >= 1,      "LDBL_MANT_DIG_min");
  CHECK(FLT_DIG    >= 6,         "FLT_DIG_min");
  CHECK(DBL_DIG    >= 10,        "DBL_DIG_min");
  CHECK(LDBL_DIG   >= 10,        "LDBL_DIG_min");
  CHECK(FLT_MIN_10_EXP  <= -37,  "FLT_MIN_10_EXP_max");
  CHECK(DBL_MIN_10_EXP  <= -37,  "DBL_MIN_10_EXP_max");
  CHECK(LDBL_MIN_10_EXP <= -37,  "LDBL_MIN_10_EXP_max");
  CHECK(FLT_MAX_10_EXP  >= 37,   "FLT_MAX_10_EXP_min");
  CHECK(DBL_MAX_10_EXP  >= 37,   "DBL_MAX_10_EXP_min");
  CHECK(LDBL_MAX_10_EXP >= 37,   "LDBL_MAX_10_EXP_min");

  CHECK(FLT_MAX   >= 1e37f,      "FLT_MAX_min");
  CHECK(DBL_MAX   >= 1e37,       "DBL_MAX_min");
  CHECK(LDBL_MAX  >= 1e37L,      "LDBL_MAX_min");

  CHECK(FLT_EPSILON   <= 1e-5f,  "FLT_EPSILON_max");
  CHECK(DBL_EPSILON   <= 1e-9,   "DBL_EPSILON_max");
  CHECK(LDBL_EPSILON  <= 1e-9L,  "LDBL_EPSILON_max");

  CHECK(FLT_MIN   <= 1e-37f,     "FLT_MIN_max");
  CHECK(DBL_MIN   <= 1e-37,      "DBL_MIN_max");
  CHECK(LDBL_MIN  <= 1e-37L,     "LDBL_MIN_max");

  CHECK(DECIMAL_DIG >= 10,       "DECIMAL_DIG_min");

  /* §5.2.4.2.2¶8: FLT_ROUNDS in -1..3 (implementation-defined). */
  int rounds = FLT_ROUNDS;
  CHECK(rounds >= -1 && rounds <= 3, "FLT_ROUNDS_range");

  /* §5.2.4.2.2¶10: *_HAS_SUBNORM in {-1, 0, 1}. */
  CHECK(FLT_HAS_SUBNORM  == -1 || FLT_HAS_SUBNORM  == 0 || FLT_HAS_SUBNORM  == 1,
        "FLT_HAS_SUBNORM_range");
  CHECK(DBL_HAS_SUBNORM  == -1 || DBL_HAS_SUBNORM  == 0 || DBL_HAS_SUBNORM  == 1,
        "DBL_HAS_SUBNORM_range");
  CHECK(LDBL_HAS_SUBNORM == -1 || LDBL_HAS_SUBNORM == 0 || LDBL_HAS_SUBNORM == 1,
        "LDBL_HAS_SUBNORM_range");

  /* FLT_EVAL_METHOD: just confirm the macro is usable as `int` in
   * `#if` and at runtime. Standard allows -1, 0, 1, 2 (+ implementation-
   * defined extensions). */
  int eval = FLT_EVAL_METHOD;
  CHECK(eval >= -1, "FLT_EVAL_METHOD_int");
}

/* ---- 3. helper-TU round-trip (LINKED-libclib view) ------------------- */
static void check_helper_int_roundtrip(void) {
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_RADIX)            == (long)FLT_RADIX,           "rt_FLT_RADIX");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_EVAL_METHOD)      == (long)FLT_EVAL_METHOD,     "rt_FLT_EVAL_METHOD");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_ROUNDS)           == (long)FLT_ROUNDS,          "rt_FLT_ROUNDS");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_FLT_HAS_SUBNORM)  == (long)FLT_HAS_SUBNORM,     "rt_FLT_HAS_SUBNORM");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_DBL_HAS_SUBNORM)  == (long)DBL_HAS_SUBNORM,     "rt_DBL_HAS_SUBNORM");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_LDBL_HAS_SUBNORM) == (long)LDBL_HAS_SUBNORM,    "rt_LDBL_HAS_SUBNORM");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_DECIMAL_DIG)      == (long)DECIMAL_DIG,         "rt_DECIMAL_DIG");

  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_FLT_MANT_DIG)     == (long)FLT_MANT_DIG,        "rt_FLT_MANT_DIG");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_DBL_MANT_DIG)     == (long)DBL_MANT_DIG,        "rt_DBL_MANT_DIG");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_LDBL_MANT_DIG)    == (long)LDBL_MANT_DIG,       "rt_LDBL_MANT_DIG");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_FLT_DECIMAL_DIG)  == (long)FLT_DECIMAL_DIG,     "rt_FLT_DECIMAL_DIG");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_DBL_DECIMAL_DIG)  == (long)DBL_DECIMAL_DIG,     "rt_DBL_DECIMAL_DIG");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_LDBL_DECIMAL_DIG) == (long)LDBL_DECIMAL_DIG,    "rt_LDBL_DECIMAL_DIG");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_FLT_DIG)          == (long)FLT_DIG,             "rt_FLT_DIG");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_DBL_DIG)          == (long)DBL_DIG,             "rt_DBL_DIG");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_LDBL_DIG)         == (long)LDBL_DIG,            "rt_LDBL_DIG");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_FLT_MIN_EXP)      == (long)FLT_MIN_EXP,         "rt_FLT_MIN_EXP");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_DBL_MIN_EXP)      == (long)DBL_MIN_EXP,         "rt_DBL_MIN_EXP");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_LDBL_MIN_EXP)     == (long)LDBL_MIN_EXP,        "rt_LDBL_MIN_EXP");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_FLT_MIN_10_EXP)   == (long)FLT_MIN_10_EXP,      "rt_FLT_MIN_10_EXP");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_DBL_MIN_10_EXP)   == (long)DBL_MIN_10_EXP,      "rt_DBL_MIN_10_EXP");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_LDBL_MIN_10_EXP)  == (long)LDBL_MIN_10_EXP,     "rt_LDBL_MIN_10_EXP");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_FLT_MAX_EXP)      == (long)FLT_MAX_EXP,         "rt_FLT_MAX_EXP");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_DBL_MAX_EXP)      == (long)DBL_MAX_EXP,         "rt_DBL_MAX_EXP");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_LDBL_MAX_EXP)     == (long)LDBL_MAX_EXP,        "rt_LDBL_MAX_EXP");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_FLT_MAX_10_EXP)   == (long)FLT_MAX_10_EXP,      "rt_FLT_MAX_10_EXP");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_DBL_MAX_10_EXP)   == (long)DBL_MAX_10_EXP,      "rt_DBL_MAX_10_EXP");
  CHECK(clib_float_eval_int(CLIB_FLOAT_OP_LDBL_MAX_10_EXP)  == (long)LDBL_MAX_10_EXP,     "rt_LDBL_MAX_10_EXP");

  /* Unknown selector returns the sentinel; this also pins that the
   * helper actually walks its switch rather than returning a
   * constant. */
  CHECK(clib_float_eval_int(9999) == (-2147483647L - 1L), "rt_helper_unknown_sentinel");

  CHECK(clib_float_int_op_count() == 28, "rt_helper_int_op_count");
}

static void check_helper_fp_roundtrip(void) {
  /* Exact bitwise equality is required: both sides cast the same
   * macro to the same long-double type, with no arithmetic in
   * between. */
  CHECK(clib_float_eval_fp(CLIB_FLOAT_FP_OP_FLT_MAX)      == (long double)FLT_MAX,     "rt_FLT_MAX");
  CHECK(clib_float_eval_fp(CLIB_FLOAT_FP_OP_DBL_MAX)      == (long double)DBL_MAX,     "rt_DBL_MAX");
  CHECK(clib_float_eval_fp(CLIB_FLOAT_FP_OP_LDBL_MAX)     == (long double)LDBL_MAX,    "rt_LDBL_MAX");
  CHECK(clib_float_eval_fp(CLIB_FLOAT_FP_OP_FLT_MIN)      == (long double)FLT_MIN,     "rt_FLT_MIN");
  CHECK(clib_float_eval_fp(CLIB_FLOAT_FP_OP_DBL_MIN)      == (long double)DBL_MIN,     "rt_DBL_MIN");
  CHECK(clib_float_eval_fp(CLIB_FLOAT_FP_OP_LDBL_MIN)     == (long double)LDBL_MIN,    "rt_LDBL_MIN");
  CHECK(clib_float_eval_fp(CLIB_FLOAT_FP_OP_FLT_EPSILON)  == (long double)FLT_EPSILON, "rt_FLT_EPSILON");
  CHECK(clib_float_eval_fp(CLIB_FLOAT_FP_OP_DBL_EPSILON)  == (long double)DBL_EPSILON, "rt_DBL_EPSILON");
  CHECK(clib_float_eval_fp(CLIB_FLOAT_FP_OP_LDBL_EPSILON) == (long double)LDBL_EPSILON,"rt_LDBL_EPSILON");

#ifdef FLT_TRUE_MIN
  CHECK(clib_float_eval_fp(CLIB_FLOAT_FP_OP_FLT_TRUE_MIN)  == (long double)FLT_TRUE_MIN,  "rt_FLT_TRUE_MIN");
#endif
#ifdef DBL_TRUE_MIN
  CHECK(clib_float_eval_fp(CLIB_FLOAT_FP_OP_DBL_TRUE_MIN)  == (long double)DBL_TRUE_MIN,  "rt_DBL_TRUE_MIN");
#endif
#ifdef LDBL_TRUE_MIN
  CHECK(clib_float_eval_fp(CLIB_FLOAT_FP_OP_LDBL_TRUE_MIN) == (long double)LDBL_TRUE_MIN, "rt_LDBL_TRUE_MIN");
#endif

  CHECK(clib_float_fp_op_count() == 12, "rt_helper_fp_op_count");
}

/* ---- 4. symbol_set_pinned (function pointers) ------------------------ */
static void check_symbol_set_pinned(void) {
  long        (*p_int)(int)    = &clib_float_eval_int;
  int         (*p_intc)(void)  = &clib_float_int_op_count;
  long double (*p_fp)(int)     = &clib_float_eval_fp;
  int         (*p_fpc)(void)   = &clib_float_fp_op_count;
  CHECK(p_int  != 0, "clib_float_eval_int_addr_nonnull");
  CHECK(p_intc != 0, "clib_float_int_op_count_addr_nonnull");
  CHECK(p_fp   != 0, "clib_float_eval_fp_addr_nonnull");
  CHECK(p_fpc  != 0, "clib_float_fp_op_count_addr_nonnull");
}

int main(void) {
  check_macros_defined();
  if (!g_fail) printf("TEST:PASS:clib_float:macros_defined\n");

  check_c11_minima();
  if (!g_fail) printf("TEST:PASS:clib_float:c11_minima\n");

  check_helper_int_roundtrip();
  if (!g_fail) printf("TEST:PASS:clib_float:helper_int_roundtrip\n");

  check_helper_fp_roundtrip();
  if (!g_fail) printf("TEST:PASS:clib_float:helper_fp_roundtrip\n");

  check_symbol_set_pinned();
  if (!g_fail) printf("TEST:PASS:clib_float:symbol_set_pinned\n");

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:clib_float\n");
    return 1;
  }
  printf("TEST:PASS:clib_float\n");
  return 0;
}
