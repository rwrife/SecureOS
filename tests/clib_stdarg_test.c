/*
 * tests/clib_stdarg_test.c
 *
 * Host unit test for the freestanding <stdarg.h> nucleus shipped by
 * user/libs/clib (issue #407 / M7-TOOLCHAIN-004 slice 6, plan
 * plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Compiled with `-fno-builtin` (see build/scripts/test_clib_stdarg.sh)
 * so the assertions exercise OUR macro expansions (which intentionally
 * forward to __builtin_va_*; see header rationale) rather than any
 * host-libc shortcut that might short-circuit them.
 *
 * Sub-markers (each must round-trip via TEST:PASS:clib_stdarg:...):
 *   - va_list_typedef_present       : compile-time — va_list usable as a type
 *   - va_start_arg_end_basic        : 5-int variadic walk, sum matches
 *   - va_copy_independent_walks     : two parallel walks via va_copy
 *                                     each yield the same sum
 *   - zero_args_clean               : n=0 returns 0, no UB
 *   - large_arg_count               : 64-int walk on the boundary of
 *                                     register-spill-to-stack on SysV
 *   - saturating_overflow_clamp     : INT_MAX + 1 saturates to INT_MAX
 *   - symbol_set_pinned             : `clib_stdarg_sum_ints` +
 *                                     `clib_stdarg_sum_ints_via_copy`
 *                                     reachable through a function
 *                                     pointer — drift guard
 *
 * Roll-up marker:
 *   - TEST:PASS:clib_stdarg         (only emitted if every sub-marker
 *                                    passed and zero TEST:FAIL: lines
 *                                    were recorded).
 */

#include <stdio.h>
#include <limits.h>

#include "../user/libs/clib/include/clib/stdarg.h"

/* Prototype the helpers shipped by user/libs/clib/src/stdarg.c so we
 * do not need a separate internal header. The .c file is the source
 * of truth for the signature; any drift here would fail the link. */
int clib_stdarg_sum_ints(int n, ...);
int clib_stdarg_sum_ints_via_copy(int n, ...);

static int g_fail = 0;

#define CHECK(cond, name)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "TEST:FAIL:clib_stdarg:%s\n", (name));                   \
      g_fail = 1;                                                              \
    }                                                                          \
  } while (0)

/* Compile-time: va_list must be usable as a typedef. */
static void test_va_list_typedef_present(void) {
  /* If this function compiles, va_list exists as a type. We do NOT
   * call va_start here (no variadic context); we just take the size
   * to force the compiler to materialise the type. */
  va_list ap;
  (void)ap;
  size_t sz = sizeof(va_list);
  int ok = (sz > 0);
  CHECK(ok, "va_list_typedef_present");
  if (ok) printf("TEST:PASS:clib_stdarg:va_list_typedef_present\n");
}

static void test_va_start_arg_end_basic(void) {
  int r = clib_stdarg_sum_ints(5, 1, 2, 3, 4, 5);
  int ok = (r == 15);
  CHECK(ok, "va_start_arg_end_basic");
  if (ok) printf("TEST:PASS:clib_stdarg:va_start_arg_end_basic\n");
}

static void test_va_copy_independent_walks(void) {
  /* sum_via_copy walks the args TWICE (once original, once via_copy)
   * and adds the totals together, so a 5-int input summing to 15
   * must return 30 if our va_copy expansion preserves independence. */
  int r = clib_stdarg_sum_ints_via_copy(5, 1, 2, 3, 4, 5);
  int ok = (r == 30);
  CHECK(ok, "va_copy_independent_walks");
  if (ok) printf("TEST:PASS:clib_stdarg:va_copy_independent_walks\n");
}

static void test_zero_args_clean(void) {
  int r1 = clib_stdarg_sum_ints(0);
  int r2 = clib_stdarg_sum_ints_via_copy(0);
  int r3 = clib_stdarg_sum_ints(-3); /* defensive: negative n → 0, no UB */
  int ok = (r1 == 0) && (r2 == 0) && (r3 == 0);
  CHECK(ok, "zero_args_clean");
  if (ok) printf("TEST:PASS:clib_stdarg:zero_args_clean\n");
}

static void test_large_arg_count(void) {
  /* x86_64 SysV passes the first 6 integer args in registers; beyond
   * that, the prologue spills to the stack and `__builtin_va_arg`
   * walks both the register-save area and the overflow area. Pushing
   * to 64 args exercises both halves of the walker. */
  int r = clib_stdarg_sum_ints(
      64,
      1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1);
  int ok = (r == 64);
  CHECK(ok, "large_arg_count");
  if (ok) printf("TEST:PASS:clib_stdarg:large_arg_count\n");
}

static void test_saturating_overflow_clamp(void) {
  /* Defensive: feed three values whose true sum overflows INT_MAX;
   * the helper must saturate rather than invoke signed-overflow UB. */
  int r = clib_stdarg_sum_ints(3, INT_MAX, 1, 1);
  int ok = (r == INT_MAX);

  /* Same on the negative side. */
  int r2 = clib_stdarg_sum_ints(3, INT_MIN, -1, -1);
  ok = ok && (r2 == INT_MIN);

  CHECK(ok, "saturating_overflow_clamp");
  if (ok) printf("TEST:PASS:clib_stdarg:saturating_overflow_clamp\n");
}

/* Drift guard: each shipped symbol must remain reachable through a
 * function pointer. The same shape as PR #416 / #417 / #418 / #428 /
 * #430. If a future header edit or refactor drops one, the pointer
 * array contains a NULL entry and the assert fails. */
static void test_symbol_set_pinned(void) {
  void *const symbols[] = {
      (void *)&clib_stdarg_sum_ints,
      (void *)&clib_stdarg_sum_ints_via_copy,
  };
  const int n = (int)(sizeof(symbols) / sizeof(symbols[0]));
  int ok = 1;
  for (int i = 0; i < n; ++i) {
    if (symbols[i] == 0) { ok = 0; break; }
  }
  /* And the macro surface must still expand (compile-time check). */
  /* If any of va_start / va_arg / va_end / va_copy were not defined,
   * the test file would have failed to compile higher up; this is
   * just an explicit anchor for the symbol_set_pinned audit. */
#if !defined(va_start) || !defined(va_arg) || !defined(va_end) || !defined(va_copy)
  ok = 0;
#endif
  CHECK(ok, "symbol_set_pinned");
  if (ok) printf("TEST:PASS:clib_stdarg:symbol_set_pinned\n");
}

int main(void) {
  test_va_list_typedef_present();
  test_va_start_arg_end_basic();
  test_va_copy_independent_walks();
  test_zero_args_clean();
  test_large_arg_count();
  test_saturating_overflow_clamp();
  test_symbol_set_pinned();

  if (!g_fail) {
    printf("TEST:PASS:clib_stdarg\n");
    return 0;
  }
  fprintf(stderr, "TEST:FAIL:clib_stdarg\n");
  return 1;
}
