/**
 * @file clib_stdbool_test.c
 * @brief Host unit test for the freestanding <stdbool.h> nucleus
 *        (issue #407 slice 9, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Covers:
 *   1. `true` / `false` fold to integer constants 1 / 0.
 *   2. `bool` is an alias for `_Bool` (sizeof 1, not int-sized).
 *   3. `__bool_true_false_are_defined` is the integer constant 1 (C11
 *      §7.18¶3).
 *   4. Values via the drift-anchor TU (`src/stdbool.c`) match the
 *      values this TU sees (so a future header drift is caught against
 *      what `libclib` actually ships, not just what this TU includes).
 *   5. Helper TU symbols (`clib_stdbool_*`) reachable via function
 *      pointer (`symbol_set_pinned`).
 *
 * Compiled with `-fno-builtin` so the assertions exercise OUR header
 * macros + helper TU rather than `__builtin_*` shortcuts. Mirrors the
 * `<limits.h>` (PR #434) and `<stdarg.h>` (PR #431) anchor strategy.
 *
 * Launched by:
 *   build/scripts/test_clib_stdbool.sh (dispatched via
 *   build/scripts/test.sh clib_stdbool).
 */

#include <stdio.h>

/* OUR freestanding header. Pulled in standalone (no host `<stdbool.h>`)
 * so the textual constants in this TU are exactly what `libclib`
 * ships. The separately-compiled src/stdbool.c TU is also linked in:
 * the `clib_stdbool_*` helpers fold the macros at THAT TU's compile
 * time, so the test verifies the macros as the LINKED `libclib` sees
 * them. */
#include "../user/libs/clib/include/clib/stdbool.h"

static int g_fail = 0;

#define CHECK(cond, name) do {                                      \
  if (!(cond)) {                                                    \
    fprintf(stderr, "TEST:FAIL:clib_stdbool:%s\n", (name));         \
    g_fail = 1;                                                     \
  }                                                                 \
} while (0)

/* ---- 1. true / false fold to 1 / 0 ------------------------------------ */
static void check_true_false_values(void) {
  CHECK((int)true  == 1, "true_eq_1_textual");
  CHECK((int)false == 0, "false_eq_0_textual");

  /* Round-trip through the helper TU so a header drift that this TU
   * re-includes would still be caught against the linked libclib. */
  CHECK(clib_stdbool_true_value()  == 1, "true_eq_1_helper");
  CHECK(clib_stdbool_false_value() == 0, "false_eq_0_helper");
}

/* ---- 2. bool is _Bool (sizeof 1, distinct from int) ------------------- */
static void check_bool_is_underscore_bool(void) {
  /* sizeof(_Bool) is required to be at least 1 by the standard; every
   * platform SecureOS supports compiles _Bool to 1 byte. If a future
   * change re-aliased `bool` to `int` (a common freestanding shortcut),
   * this assertion would fire. */
  CHECK(sizeof(bool) == 1, "sizeof_bool_eq_1_textual");
  CHECK(clib_stdbool_sizeof_bool() == 1, "sizeof_bool_eq_1_helper");

  /* _Bool's narrowing semantics: any non-zero stored as 1. */
  bool b1 = (bool)42;
  bool b2 = (bool)0;
  bool b3 = (bool)-1;
  CHECK(b1 == 1, "bool_narrows_nonzero_to_1");
  CHECK(b2 == 0, "bool_zero_stays_zero");
  CHECK(b3 == 1, "bool_negative_narrows_to_1");
}

/* ---- 3. __bool_true_false_are_defined == 1 ---------------------------- */
static void check_feature_macro(void) {
  CHECK(__bool_true_false_are_defined == 1,
        "feature_macro_eq_1_textual");
  CHECK(clib_stdbool_feature_macro_value() == 1,
        "feature_macro_eq_1_helper");
}

/* ---- 4. usability in real boolean contexts ---------------------------- */
static void check_usable_in_control_flow(void) {
  bool seen_true = false;
  bool seen_false = false;
  for (int i = 0; i < 4; i++) {
    bool even = ((i & 1) == 0);
    if (even)  seen_true  = true;
    if (!even) seen_false = true;
  }
  CHECK(seen_true,  "bool_drives_if_true_branch");
  CHECK(seen_false, "bool_drives_if_false_branch");

  /* true && false short-circuits the way the standard requires. */
  int trips = 0;
  if (true && false) { trips++; }
  if (false || true) { trips++; }
  CHECK(trips == 1, "true_false_logical_ops");
}

/* ---- 5. symbol_set_pinned (helper TUs reachable via function ptr) ----- */
static void check_symbol_set_pinned(void) {
  int (*p_true) (void) = clib_stdbool_true_value;
  int (*p_false)(void) = clib_stdbool_false_value;
  int (*p_size) (void) = clib_stdbool_sizeof_bool;
  int (*p_feat) (void) = clib_stdbool_feature_macro_value;

  CHECK(p_true  != NULL, "symbol_true_value_present");
  CHECK(p_false != NULL, "symbol_false_value_present");
  CHECK(p_size  != NULL, "symbol_sizeof_bool_present");
  CHECK(p_feat  != NULL, "symbol_feature_macro_present");

  CHECK(p_true()  == 1, "symbol_true_value_callable");
  CHECK(p_false() == 0, "symbol_false_value_callable");
  CHECK(p_size()  == 1, "symbol_sizeof_bool_callable");
  CHECK(p_feat()  == 1, "symbol_feature_macro_callable");
}

int main(void) {
  fprintf(stdout, "TEST:START:clib_stdbool\n");

  check_true_false_values();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stdbool:true_false_values\n");

  int prev_fail = g_fail;
  check_bool_is_underscore_bool();
  if (g_fail == prev_fail) {
    fprintf(stdout, "TEST:PASS:clib_stdbool:bool_is_underscore_bool\n");
  }

  prev_fail = g_fail;
  check_feature_macro();
  if (g_fail == prev_fail) {
    fprintf(stdout, "TEST:PASS:clib_stdbool:feature_macro\n");
  }

  prev_fail = g_fail;
  check_usable_in_control_flow();
  if (g_fail == prev_fail) {
    fprintf(stdout, "TEST:PASS:clib_stdbool:usable_in_control_flow\n");
  }

  prev_fail = g_fail;
  check_symbol_set_pinned();
  if (g_fail == prev_fail) {
    fprintf(stdout, "TEST:PASS:clib_stdbool:symbol_set_pinned\n");
  }

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:clib_stdbool\n");
    return 1;
  }
  fprintf(stdout, "TEST:PASS:clib_stdbool\n");
  return 0;
}
