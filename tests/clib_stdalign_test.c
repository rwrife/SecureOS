/**
 * @file clib_stdalign_test.c
 * @brief Host unit test for the freestanding <stdalign.h> nucleus
 *        (issue #407 slice, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Covers:
 *   1. All 4 macros required by C11 §7.15¶1-2 are #defined.
 *   2. `alignas` and `alignof` expand to the corresponding C11 keywords
 *      `_Alignas` / `_Alignof`: verified semantically by computing
 *      `alignof(T)` for several scalar types both in the test TU
 *      (through OUR header) and in the helper TU (`src/stdalign.c`)
 *      and checking the two agree. The helper TU pins the LINKED
 *      libclib view; the textual check pins what THIS TU's include
 *      sees. The test also forces `alignas(N)` into a real struct
 *      declaration and verifies the field's alignment.
 *   3. The feature-test macros `__alignas_is_defined` and
 *      `__alignof_is_defined` both equal the integer constant `1`
 *      mandated by §7.15¶2.
 *   4. The helper TU exports `clib_stdalign_eval` and
 *      `clib_stdalign_op_count` (`symbol_set_pinned`).
 *
 * Compiled with `-fno-builtin`. We deliberately do NOT include the
 * host `<stdalign.h>`; under `-Werror` that would either collide with
 * OUR header or mask a missing definition.
 *
 * Launched by:
 *   build/scripts/test_clib_stdalign.sh (dispatched via
 *   build/scripts/test.sh clib_stdalign).
 */

#include <stdio.h>

#include "../user/libs/clib/include/clib/stdalign.h"

/* Selector enum + helper-TU prototypes; kept in sync by hand with
 * src/stdalign.c (test would fail at link time if either drifts). */
enum {
  CLIB_STDALIGN_OP_ALIGNAS_IS_DEFINED = 0,
  CLIB_STDALIGN_OP_ALIGNOF_IS_DEFINED = 1,
  CLIB_STDALIGN_OP_ALIGNOF_CHAR       = 2,
  CLIB_STDALIGN_OP_ALIGNOF_INT        = 3,
  CLIB_STDALIGN_OP_ALIGNOF_DOUBLE     = 4,
  CLIB_STDALIGN_OP_ALIGNOF_LONG       = 5,
  CLIB_STDALIGN_OP_ALIGNAS_FIELD_16   = 6,
  CLIB_STDALIGN_OP_ALIGNAS_FIELD_32   = 7,
  CLIB_STDALIGN_OP__COUNT             = 8
};

unsigned long clib_stdalign_eval(int op);
int           clib_stdalign_op_count(void);

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:clib_stdalign:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* ---- 1. all 4 macros are defined -------------------------------------- */
static void check_macros_defined(void) {
#ifndef alignas
  CHECK(0, "alignas_not_defined");
#endif
#ifndef alignof
  CHECK(0, "alignof_not_defined");
#endif
#ifndef __alignas_is_defined
  CHECK(0, "__alignas_is_defined_not_defined");
#endif
#ifndef __alignof_is_defined
  CHECK(0, "__alignof_is_defined_not_defined");
#endif
  (void)g_fail; /* silence "unused" diagnostics on the all-success path */
}

/* ---- 2. alignas / alignof expand to the corresponding keywords ------- */
/* Local struct that uses `alignas` through OUR header. If `alignas`
 * doesn't expand to `_Alignas` (or another spelling the compiler
 * accepts as an alignment specifier in this position), this TU fails
 * to compile under -Werror. */
struct local_aligned16 {
  char pad;
  alignas(16) char a;
};

static void check_macros_expand_correctly(void) {
  /* alignof(scalars) must be positive and match _Alignof(T). The
   * standard guarantees these are equivalent; we use _Alignof on the
   * RHS so the comparison is independent of any host alignof macro. */
  CHECK(alignof(char)   == _Alignof(char),   "alignof_char_matches");
  CHECK(alignof(int)    == _Alignof(int),    "alignof_int_matches");
  CHECK(alignof(double) == _Alignof(double), "alignof_double_matches");
  CHECK(alignof(long)   == _Alignof(long),   "alignof_long_matches");

  /* alignas(N) must actually align a field to N. */
  CHECK(_Alignof(struct local_aligned16) == 16, "alignas_16_observed");
}

/* ---- 3. feature-test macros are the integer constant 1 --------------- */
static void check_feature_test_macros(void) {
  /* §7.15¶2: both MUST expand to the integer constant 1. */
  CHECK(__alignas_is_defined == 1, "alignas_is_defined_eq_1");
  CHECK(__alignof_is_defined == 1, "alignof_is_defined_eq_1");

  /* Usable in `#if`. We cannot test the preprocessor branch at
   * runtime, but a non-`1` value would have failed the constants
   * above anyway. */
#if !__alignas_is_defined
  CHECK(0, "alignas_is_defined_false_in_pp");
#endif
#if !__alignof_is_defined
  CHECK(0, "alignof_is_defined_false_in_pp");
#endif
}

/* ---- 4. helper-TU agreement (LINKED-libclib view) -------------------- */
static void check_helper_tu_agrees(void) {
  /* The helper TU folds each macro at its OWN compile time. If a
   * future regression rewrites `alignas` or `alignof` to a different
   * keyword in OUR header, the helper TU sees the new value but the
   * test TU also sees it via the same include — so the textual checks
   * above could miss a drift that affects only one TU's preprocessor
   * state. Linking the helper TU's results back here pins both. */
  CHECK(clib_stdalign_eval(CLIB_STDALIGN_OP_ALIGNAS_IS_DEFINED) == 1u,
        "helper_alignas_is_defined");
  CHECK(clib_stdalign_eval(CLIB_STDALIGN_OP_ALIGNOF_IS_DEFINED) == 1u,
        "helper_alignof_is_defined");

  CHECK(clib_stdalign_eval(CLIB_STDALIGN_OP_ALIGNOF_CHAR) ==
        (unsigned long)_Alignof(char),   "helper_alignof_char");
  CHECK(clib_stdalign_eval(CLIB_STDALIGN_OP_ALIGNOF_INT) ==
        (unsigned long)_Alignof(int),    "helper_alignof_int");
  CHECK(clib_stdalign_eval(CLIB_STDALIGN_OP_ALIGNOF_DOUBLE) ==
        (unsigned long)_Alignof(double), "helper_alignof_double");
  CHECK(clib_stdalign_eval(CLIB_STDALIGN_OP_ALIGNOF_LONG) ==
        (unsigned long)_Alignof(long),   "helper_alignof_long");

  /* The helper TU declares structs with alignas(16) / alignas(32);
   * pin the observed struct alignment to prove `alignas` survived
   * the round-trip into a real declaration. */
  CHECK(clib_stdalign_eval(CLIB_STDALIGN_OP_ALIGNAS_FIELD_16) == 16u,
        "helper_alignas_field_16");
  CHECK(clib_stdalign_eval(CLIB_STDALIGN_OP_ALIGNAS_FIELD_32) == 32u,
        "helper_alignas_field_32");

  /* Unknown selector returns -1 (cast through unsigned long). */
  CHECK(clib_stdalign_eval(999) == (unsigned long)-1,
        "helper_unknown_returns_neg1");
}

/* ---- 5. symbol_set_pinned (function pointers) ------------------------ */
static void check_symbol_set_pinned(void) {
  /* Take addresses so the linker is forced to resolve every helper-TU
   * export. A future drift that drops one symbol fails at link time. */
  unsigned long (*p_eval)(int)   = &clib_stdalign_eval;
  int           (*p_count)(void) = &clib_stdalign_op_count;
  CHECK(p_eval  != 0, "clib_stdalign_eval_addr_nonnull");
  CHECK(p_count != 0, "clib_stdalign_op_count_addr_nonnull");
  CHECK(p_count() == 8, "clib_stdalign_op_count_eq_8");
}

int main(void) {
  check_macros_defined();
  if (!g_fail) printf("TEST:PASS:clib_stdalign:macros_defined\n");

  check_macros_expand_correctly();
  if (!g_fail) printf("TEST:PASS:clib_stdalign:macros_expand_correctly\n");

  check_feature_test_macros();
  if (!g_fail) printf("TEST:PASS:clib_stdalign:feature_test_macros\n");

  check_helper_tu_agrees();
  if (!g_fail) printf("TEST:PASS:clib_stdalign:helper_tu_agrees\n");

  check_symbol_set_pinned();
  if (!g_fail) printf("TEST:PASS:clib_stdalign:symbol_set_pinned\n");

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:clib_stdalign\n");
    return 1;
  }
  printf("TEST:PASS:clib_stdalign\n");
  return 0;
}
