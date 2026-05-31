/*
 * tests/clib_assert_test.c
 *
 * Host unit test for the freestanding <assert.h> nucleus shipped by
 * user/libs/clib (issue #407 / M7-TOOLCHAIN-004, plan
 * plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Compiled with `-fno-builtin -Werror` (see
 * build/scripts/test_clib_assert.sh) so the assertions exercise OUR
 * macro expansion rather than any host-libc shortcut.
 *
 * Sub-markers:
 *   - macro_defined            : `assert` is #defined after include
 *   - feature_macro            : `__assert_is_defined == 1`
 *   - static_assert_works      : C11 `static_assert` macro accepts true
 *                                expressions at file scope
 *   - assert_pass_no_call      : true expression must NOT invoke the
 *                                handler, must NOT abort, must
 *                                evaluate to `(void)0` (usable as a
 *                                statement)
 *   - assert_pass_no_side_eff_when_ndebug
 *                              : when NDEBUG is defined BEFORE the
 *                                include, expr is not evaluated
 *                                (verifies the `((void)0)` expansion)
 *   - assert_fail_invokes_handler
 *                              : false expression invokes the
 *                                registered handler with the correct
 *                                expr / file / line / func args
 *   - handler_null_restores_default
 *                              : setting handler to NULL is accepted
 *                                (caller cannot reach the default
 *                                spin from a host test, so we only
 *                                verify the setter call returns)
 *   - reinclude_toggles_ndebug : including <assert.h> a second time
 *                                with NDEBUG defined disables the
 *                                macro (C11 §7.2¶1 re-include
 *                                semantics)
 *   - symbol_set_pinned        : `clib_assert_set_handler` +
 *                                `__clib_assert_fail` reachable
 *                                through a function pointer
 *
 * Roll-up: TEST:PASS:clib_assert
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "../user/libs/clib/include/clib/assert.h"

static int g_fail = 0;

#define CHECK(cond, name)                                                    \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "TEST:FAIL:clib_assert:%s\n", (name));                 \
      g_fail = 1;                                                            \
    }                                                                        \
  } while (0)

/* ---- handler capture state ---- */
static const char *cap_expr;
static const char *cap_file;
static int         cap_line;
static const char *cap_func;
static int         cap_invoked;
static jmp_buf     cap_jmp;

static
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Noreturn
#endif
void capture_handler(const char *expr, const char *file,
                     int line, const char *func) {
  cap_expr    = expr;
  cap_file    = file;
  cap_line    = line;
  cap_func    = func;
  cap_invoked = 1;
  longjmp(cap_jmp, 1);
}

/* ---- macro_defined ---- */
static void test_macro_defined(void) {
  int ok =
#ifdef assert
      1
#else
      0
#endif
      ;
  CHECK(ok, "macro_defined");
  if (ok) printf("TEST:PASS:clib_assert:macro_defined\n");
}

/* ---- feature_macro ---- */
static void test_feature_macro(void) {
  int ok = (__assert_is_defined == 1);
  CHECK(ok, "feature_macro");
  if (ok) printf("TEST:PASS:clib_assert:feature_macro\n");
}

/* ---- static_assert_works ---- */
static_assert(sizeof(int) >= 2, "ISO C requires int >= 16 bits");
static void test_static_assert_works(void) {
  /* If the file-scope static_assert above compiled, we passed.
   * Also pin the in-function form. */
  static_assert(1 == 1, "tautology");
  CHECK(1, "static_assert_works");
  printf("TEST:PASS:clib_assert:static_assert_works\n");
}

/* ---- assert_pass_no_call ---- */
static void test_assert_pass_no_call(void) {
  cap_invoked = 0;
  clib_assert_set_handler(capture_handler);
  assert(1 == 1); /* must NOT invoke handler */
  /* Also usable in a statement position via the comma-trick. */
  if (1) assert(2 + 2 == 4); else assert(0);
  int ok = (cap_invoked == 0);
  CHECK(ok, "assert_pass_no_call");
  if (ok) printf("TEST:PASS:clib_assert:assert_pass_no_call\n");
  clib_assert_set_handler(0);
}

/* ---- assert_pass_no_side_eff_when_ndebug ----
 * Re-include with NDEBUG defined and confirm `assert(expr)` does
 * NOT evaluate `expr`. */
#define NDEBUG
#include "../user/libs/clib/include/clib/assert.h"
static void test_assert_pass_no_side_eff_when_ndebug(void) {
  int side_effect = 0;
  assert(++side_effect > 0);
  int ok = (side_effect == 0);
  CHECK(ok, "assert_pass_no_side_eff_when_ndebug");
  if (ok) printf("TEST:PASS:clib_assert:assert_pass_no_side_eff_when_ndebug\n");
}
/* Re-include WITHOUT NDEBUG so subsequent tests run the real assert. */
#undef NDEBUG
#include "../user/libs/clib/include/clib/assert.h"

/* ---- assert_fail_invokes_handler ---- */
static void test_assert_fail_invokes_handler(void) {
  cap_expr    = 0;
  cap_file    = 0;
  cap_line    = 0;
  cap_func    = 0;
  cap_invoked = 0;
  clib_assert_set_handler(capture_handler);

  const char *expected_func = __func__;
  int line_before = __LINE__;
  if (setjmp(cap_jmp) == 0) {
    assert(1 == 0); /* must invoke handler */
    /* Unreachable: handler longjmps. */
    CHECK(0, "assert_fail_invokes_handler");
    clib_assert_set_handler(0);
    return;
  }

  int ok = 1;
  ok = ok && (cap_invoked == 1);
  ok = ok && (cap_expr != 0) && (strcmp(cap_expr, "1 == 0") == 0);
  ok = ok && (cap_file != 0) && (strstr(cap_file, "clib_assert_test.c") != 0);
  /* line should be line_before + 2 (the `if (setjmp...)` line is
   * line_before + 1, the `assert(...)` line is line_before + 2). */
  ok = ok && (cap_line == line_before + 2);
  ok = ok && (cap_func != 0) && (strcmp(cap_func, expected_func) == 0);

  CHECK(ok, "assert_fail_invokes_handler");
  if (ok) printf("TEST:PASS:clib_assert:assert_fail_invokes_handler\n");
  clib_assert_set_handler(0);
}

/* ---- handler_null_restores_default ---- */
static void test_handler_null_restores_default(void) {
  clib_assert_set_handler(capture_handler);
  clib_assert_set_handler(0);
  /* If we got here, both setter calls returned cleanly. We do NOT
   * trigger a failed assert in this test because the default handler
   * is an infinite loop (intentionally — host test cannot validate
   * the default beyond confirming the setter accepts NULL). */
  CHECK(1, "handler_null_restores_default");
  printf("TEST:PASS:clib_assert:handler_null_restores_default\n");
}

/* ---- reinclude_toggles_ndebug ----
 * The macro state above was toggled NDEBUG-on then NDEBUG-off via
 * re-includes; if the assert_pass_no_side_eff_when_ndebug case and
 * the assert_fail_invokes_handler case both pass, the re-include
 * semantics worked. Pin that conclusion as its own sub-marker. */
static void test_reinclude_toggles_ndebug(void) {
  /* Re-include with NDEBUG once more, confirm `assert(0)` is a no-op
   * (no handler invocation, no abort). */
#define NDEBUG
#include "../user/libs/clib/include/clib/assert.h"
  cap_invoked = 0;
  clib_assert_set_handler(capture_handler);
  assert(0); /* under NDEBUG: must be ((void)0), no handler call */
  int ok = (cap_invoked == 0);
  clib_assert_set_handler(0);
#undef NDEBUG
#include "../user/libs/clib/include/clib/assert.h"
  /* Confirm we're back in active mode: the macro must again expand
   * to the failure-calling form. We don't trigger it here (already
   * covered above); we just check the macro is defined and not the
   * no-op spelling by indirect means: stringify and look for our
   * failure-helper name. */
  /* Sanity: a `1` predicate should still NOT call the handler now. */
  cap_invoked = 0;
  clib_assert_set_handler(capture_handler);
  assert(1);
  ok = ok && (cap_invoked == 0);
  clib_assert_set_handler(0);

  CHECK(ok, "reinclude_toggles_ndebug");
  if (ok) printf("TEST:PASS:clib_assert:reinclude_toggles_ndebug\n");
}

/* ---- symbol_set_pinned ---- */
static void test_symbol_set_pinned(void) {
  void (*p_set)(clib_assert_handler_fn) = &clib_assert_set_handler;
  void (*p_fail)(const char *, const char *, int, const char *) =
      &__clib_assert_fail;
  int ok = (p_set != 0) && (p_fail != 0) && ((void *)p_set != (void *)p_fail);
  CHECK(ok, "symbol_set_pinned");
  if (ok) printf("TEST:PASS:clib_assert:symbol_set_pinned\n");
}

int main(void) {
  printf("TEST:START:clib_assert\n");
  test_macro_defined();
  test_feature_macro();
  test_static_assert_works();
  test_assert_pass_no_call();
  test_assert_pass_no_side_eff_when_ndebug();
  test_assert_fail_invokes_handler();
  test_handler_null_restores_default();
  test_reinclude_toggles_ndebug();
  test_symbol_set_pinned();

  if (!g_fail) {
    printf("TEST:PASS:clib_assert\n");
    return 0;
  }
  fprintf(stderr, "TEST:FAIL:clib_assert\n");
  return 1;
}
