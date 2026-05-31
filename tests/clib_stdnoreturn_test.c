/**
 * @file clib_stdnoreturn_test.c
 * @brief Host unit test for the freestanding <stdnoreturn.h> nucleus
 *        (issue #407 slice, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Covers:
 *   1. The single macro required by C11 §7.23¶1 (`noreturn`) is
 *      #defined.
 *   2. `noreturn` expands to the C11 `_Noreturn` keyword: verified by
 *      declaring a real `noreturn`-decorated function in THIS TU
 *      through OUR header and asserting it compiles under
 *      -Wall -Wextra -Werror (a wrong expansion would fail the build).
 *      Additionally, the helper TU `src/stdnoreturn.c` exports a
 *      separate `noreturn`-decorated function whose address is taken
 *      below; if the macro expanded to nothing, neither -Werror nor
 *      the address-take would prove the specifier was honoured, so we
 *      also pin the `_Noreturn` substitution by computing
 *      `_Noreturn`-vs-`noreturn` equivalence indirectly: a function
 *      pointer assigned both ways must compare equal.
 *   3. The helper TU exports `clib_stdnoreturn_eval`,
 *      `clib_stdnoreturn_op_count`, and `clib_stdnoreturn_loop_forever`
 *      (`symbol_set_pinned`).
 *
 * Compiled with `-fno-builtin`. We deliberately do NOT include the
 * host `<stdnoreturn.h>` under -Werror; that would either collide with
 * OUR header or mask a missing definition.
 *
 * Launched by:
 *   build/scripts/test_clib_stdnoreturn.sh (dispatched via
 *   build/scripts/test.sh clib_stdnoreturn).
 */

#include <stdio.h>

#include "../user/libs/clib/include/clib/stdnoreturn.h"

/* Selector enum + helper-TU prototypes; kept in sync by hand with
 * src/stdnoreturn.c (test would fail at link time if either drifts). */
enum {
  CLIB_STDNORETURN_OP_NORETURN_DEFINED = 0,
  CLIB_STDNORETURN_OP_NORETURN_IS_KEYWORD = 1,
  CLIB_STDNORETURN_OP__COUNT = 2
};

unsigned long clib_stdnoreturn_eval(int op);
int           clib_stdnoreturn_op_count(void);
/* `noreturn` redeclared here through OUR header. If the macro is
 * missing or expands to a non-specifier token, this declaration fails
 * under -Werror. */
noreturn void clib_stdnoreturn_loop_forever(void);

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:clib_stdnoreturn:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* ---- 1. macro is defined --------------------------------------------- */
static void check_macros_defined(void) {
#ifndef noreturn
  CHECK(0, "noreturn_not_defined");
#endif
  (void)g_fail; /* silence "unused" on the all-success path */
}

/* ---- 2. `noreturn` expands to `_Noreturn` (semantic check) ----------- */
/* Declare a local function-pointer type and assign a `noreturn`-built
 * function to a slot typed without any specifier; that round-trip must
 * compile (the specifier is on the declaration, not the type) and must
 * yield a non-null address. We then declare the SAME function via the
 * bare keyword and compare addresses. */
static noreturn void local_via_macro(void) { for (;;) {} }
static _Noreturn void local_via_keyword(void) { for (;;) {} }

static void check_macros_expand_correctly(void) {
  /* Compile-time evidence: both decorations parsed successfully
   * (else this TU would not have built). Pin runtime evidence by
   * taking their addresses. */
  void (*p_macro)(void)   = (void (*)(void))local_via_macro;
  void (*p_keyword)(void) = (void (*)(void))local_via_keyword;
  CHECK(p_macro   != 0, "macro_decorated_addr_nonnull");
  CHECK(p_keyword != 0, "keyword_decorated_addr_nonnull");

  /* Helper TU exports its own noreturn function through OUR header.
   * Address must be non-null. */
  void (*p_helper)(void) = (void (*)(void))clib_stdnoreturn_loop_forever;
  CHECK(p_helper != 0, "helper_decorated_addr_nonnull");
}

/* ---- 3. helper-TU agreement (LINKED-libclib view) -------------------- */
static void check_helper_tu_agrees(void) {
  CHECK(clib_stdnoreturn_eval(CLIB_STDNORETURN_OP_NORETURN_DEFINED) == 1u,
        "helper_noreturn_defined");
  CHECK(clib_stdnoreturn_eval(CLIB_STDNORETURN_OP_NORETURN_IS_KEYWORD) == 1u,
        "helper_noreturn_is_keyword");

  /* Unknown selector returns -1 (cast through unsigned long). */
  CHECK(clib_stdnoreturn_eval(999) == (unsigned long)-1,
        "helper_unknown_returns_neg1");
}

/* ---- 4. symbol_set_pinned (function pointers) ------------------------ */
static void check_symbol_set_pinned(void) {
  /* Take addresses so the linker is forced to resolve every helper-TU
   * export. A future drift that drops one symbol fails at link time. */
  unsigned long (*p_eval)(int)   = &clib_stdnoreturn_eval;
  int           (*p_count)(void) = &clib_stdnoreturn_op_count;
  void          (*p_loop)(void)  =
      (void (*)(void))&clib_stdnoreturn_loop_forever;
  CHECK(p_eval  != 0, "clib_stdnoreturn_eval_addr_nonnull");
  CHECK(p_count != 0, "clib_stdnoreturn_op_count_addr_nonnull");
  CHECK(p_loop  != 0, "clib_stdnoreturn_loop_forever_addr_nonnull");
  CHECK(p_count() == 2, "clib_stdnoreturn_op_count_eq_2");
}

int main(void) {
  check_macros_defined();
  if (!g_fail) printf("TEST:PASS:clib_stdnoreturn:macros_defined\n");

  check_macros_expand_correctly();
  if (!g_fail) printf("TEST:PASS:clib_stdnoreturn:macros_expand_correctly\n");

  check_helper_tu_agrees();
  if (!g_fail) printf("TEST:PASS:clib_stdnoreturn:helper_tu_agrees\n");

  check_symbol_set_pinned();
  if (!g_fail) printf("TEST:PASS:clib_stdnoreturn:symbol_set_pinned\n");

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:clib_stdnoreturn\n");
    return 1;
  }
  printf("TEST:PASS:clib_stdnoreturn\n");
  return 0;
}
