/**
 * @file clib_iso646_test.c
 * @brief Host unit test for the freestanding <iso646.h> nucleus
 *        (issue #407 slice, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Covers:
 *   1. All 11 macros required by C11 §7.9¶1 are #defined.
 *   2. Each macro expands to the operator the standard mandates:
 *      verified by computing the operator both directly (in the test
 *      TU through OUR header) and via the helper TU `src/iso646.c`,
 *      and checking the two agree bit-for-bit. The helper TU pins the
 *      LINKED libclib view; the textual check pins what THIS TU's
 *      include sees.
 *   3. Each macro is usable as a token in real expressions (not just
 *      string-equal to the operator text) — exercised by the
 *      assignment-form macros (`and_eq`, `or_eq`, `xor_eq`) which
 *      MUST parse as `&=` / `|=` / `^=` against an lvalue.
 *   4. The helper TU exports `clib_iso646_eval` and
 *      `clib_iso646_op_count` (`symbol_set_pinned`).
 *
 * Compiled with `-fno-builtin`. We deliberately do NOT include the
 * host `<iso646.h>` — under -Werror that would either collide with
 * OUR header or, worse, mask a missing definition.
 *
 * Launched by:
 *   build/scripts/test_clib_iso646.sh (dispatched via
 *   build/scripts/test.sh clib_iso646).
 */

#include <stdio.h>

#include "../user/libs/clib/include/clib/iso646.h"

/* Selector enum + helper-TU prototypes; kept in sync by hand with
 * src/iso646.c (test would fail at link time if either drifts). */
enum {
  CLIB_ISO646_OP_AND        = 0,
  CLIB_ISO646_OP_AND_FALSE  = 1,
  CLIB_ISO646_OP_AND_EQ     = 2,
  CLIB_ISO646_OP_BITAND     = 3,
  CLIB_ISO646_OP_BITAND_HI  = 4,
  CLIB_ISO646_OP_BITOR      = 5,
  CLIB_ISO646_OP_COMPL      = 6,
  CLIB_ISO646_OP_NOT_TRUE   = 7,
  CLIB_ISO646_OP_NOT_FALSE  = 8,
  CLIB_ISO646_OP_NOT_EQ_T   = 9,
  CLIB_ISO646_OP_NOT_EQ_F   = 10,
  CLIB_ISO646_OP_OR_TRUE    = 11,
  CLIB_ISO646_OP_OR_FALSE   = 12,
  CLIB_ISO646_OP_OR_EQ      = 13,
  CLIB_ISO646_OP_XOR        = 14,
  CLIB_ISO646_OP_XOR_EQ     = 15,
  CLIB_ISO646_OP__COUNT     = 16
};

unsigned long clib_iso646_eval(int op);
int           clib_iso646_op_count(void);

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:clib_iso646:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* ---- 1. all 11 macros are defined ------------------------------------- */
static void check_macros_defined(void) {
#ifndef and
  CHECK(0, "and_not_defined");
#endif
#ifndef and_eq
  CHECK(0, "and_eq_not_defined");
#endif
#ifndef bitand
  CHECK(0, "bitand_not_defined");
#endif
#ifndef bitor
  CHECK(0, "bitor_not_defined");
#endif
#ifndef compl
  CHECK(0, "compl_not_defined");
#endif
#ifndef not
  CHECK(0, "not_not_defined");
#endif
#ifndef not_eq
  CHECK(0, "not_eq_not_defined");
#endif
#ifndef or
  CHECK(0, "or_not_defined");
#endif
#ifndef or_eq
  CHECK(0, "or_eq_not_defined");
#endif
#ifndef xor
  CHECK(0, "xor_not_defined");
#endif
#ifndef xor_eq
  CHECK(0, "xor_eq_not_defined");
#endif
  /* Silence "unused" diagnostics from -Wall + the all-success path. */
  (void)g_fail;
}

/* ---- 2. each macro expands to the correct operator (textual TU) ------- */
static void check_macros_expand_correctly(void) {
  /* Logical AND. */
  CHECK((1 and 1) == 1, "and_true");
  CHECK((1 and 0) == 0, "and_false");

  /* Bitwise AND. */
  CHECK((0xF0u bitand 0x0Fu) == 0x00u, "bitand_zero");
  CHECK((0xFFu bitand 0xF0u) == 0xF0u, "bitand_mask");

  /* Bitwise OR. */
  CHECK((0xF0u bitor 0x0Fu) == 0xFFu, "bitor_combine");

  /* Bitwise complement. Compare against the same expression compiled
   * with the bare operator to make the test independent of `unsigned
   * int` width. */
  CHECK((unsigned int)(compl 0u) == (unsigned int)(~0u), "compl_all_ones");

  /* Logical NOT. */
  CHECK((not 1) == 0, "not_true");
  CHECK((not 0) == 1, "not_false");

  /* Inequality. */
  CHECK((1 not_eq 2) == 1, "not_eq_true");
  CHECK((7 not_eq 7) == 0, "not_eq_false");

  /* Logical OR. */
  CHECK((0 or 1) == 1, "or_true");
  CHECK((0 or 0) == 0, "or_false");

  /* Bitwise XOR. */
  CHECK((0xFFu xor 0x0Fu) == 0xF0u, "xor_mask");
}

/* ---- 3. assignment-form macros parse as compound assignments ---------- */
static void check_assignment_forms(void) {
  unsigned a;

  a = 0x0Fu; a and_eq 0xF0u; CHECK(a == 0x00u, "and_eq_assigns");
  a = 0xF0u; a or_eq  0x0Fu; CHECK(a == 0xFFu, "or_eq_assigns");
  a = 0xF0u; a xor_eq 0xFFu; CHECK(a == 0x0Fu, "xor_eq_assigns");
}

/* ---- 4. helper-TU agreement (LINKED-libclib view) --------------------- */
static void check_helper_tu_agrees(void) {
  /* Same operations, folded at the helper TU's compile time. If a
   * future regression changes (e.g.) `#define or  &&` -> `#define or
   * &`, this TU still computes the correct value via its own include
   * — but the helper TU now disagrees, and the link-time round-trip
   * flips this test. */
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_AND)        == 1u,        "helper_and");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_AND_FALSE)  == 0u,        "helper_and_false");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_AND_EQ)     == 0x00u,     "helper_and_eq");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_BITAND)     == 0x00u,     "helper_bitand");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_BITAND_HI)  == 0xF0u,     "helper_bitand_hi");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_BITOR)      == 0xFFu,     "helper_bitor");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_COMPL)      ==
        (unsigned long)(unsigned int)~0u,                          "helper_compl");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_NOT_TRUE)   == 0u,        "helper_not_true");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_NOT_FALSE)  == 1u,        "helper_not_false");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_NOT_EQ_T)   == 1u,        "helper_not_eq_true");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_NOT_EQ_F)   == 0u,        "helper_not_eq_false");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_OR_TRUE)    == 1u,        "helper_or_true");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_OR_FALSE)   == 0u,        "helper_or_false");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_OR_EQ)      == 0xFFu,     "helper_or_eq");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_XOR)        == 0xF0u,     "helper_xor");
  CHECK(clib_iso646_eval(CLIB_ISO646_OP_XOR_EQ)     == 0x0Fu,     "helper_xor_eq");

  /* Unknown selector returns -1 (cast through unsigned long). */
  CHECK(clib_iso646_eval(999) == (unsigned long)-1, "helper_unknown_returns_neg1");
}

/* ---- 5. symbol_set_pinned (function pointers) ------------------------- */
static void check_symbol_set_pinned(void) {
  /* Take addresses so the linker is forced to resolve every helper-TU
   * export. A future drift that drops one symbol fails at link time. */
  unsigned long (*p_eval)(int)  = &clib_iso646_eval;
  int           (*p_count)(void) = &clib_iso646_op_count;
  CHECK(p_eval  != 0, "clib_iso646_eval_addr_nonnull");
  CHECK(p_count != 0, "clib_iso646_op_count_addr_nonnull");
  CHECK(p_count() == 16, "clib_iso646_op_count_eq_16");
}

int main(void) {
  check_macros_defined();
  if (!g_fail) printf("TEST:PASS:clib_iso646:macros_defined\n");

  check_macros_expand_correctly();
  if (!g_fail) printf("TEST:PASS:clib_iso646:macros_expand_correctly\n");

  check_assignment_forms();
  if (!g_fail) printf("TEST:PASS:clib_iso646:assignment_forms_parse\n");

  check_helper_tu_agrees();
  if (!g_fail) printf("TEST:PASS:clib_iso646:helper_tu_agrees\n");

  check_symbol_set_pinned();
  if (!g_fail) printf("TEST:PASS:clib_iso646:symbol_set_pinned\n");

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:clib_iso646\n");
    return 1;
  }
  printf("TEST:PASS:clib_iso646\n");
  return 0;
}
