/**
 * @file src/stdbool.c
 * @brief Drift-anchor TU for the freestanding <stdbool.h> nucleus
 *        (issue #407 slice 9). Each helper folds a macro from
 *        `include/clib/stdbool.h` at THIS TU's compile time, so the
 *        host test round-trips the linked `libclib` value rather than
 *        only the textual constant the test TU happened to include.
 *
 * Implementation notes:
 *   - No syscalls, no globals (beyond compile-time-folded literals).
 *   - Pure freestanding: no `<stdint.h>` / `<stddef.h>` needed.
 *   - Same shape as `src/limits.c` (PR #434) and `src/stdarg.c`
 *     (PR #431) — keeps `symbol_set_pinned` honest for a macro-only
 *     header.
 */

#include "../include/clib/stdbool.h"

int clib_stdbool_true_value(void) {
  /* true is required to be an integer constant equal to 1 (C11 §7.18). */
  return (int)true;
}

int clib_stdbool_false_value(void) {
  /* false is required to be an integer constant equal to 0 (C11 §7.18). */
  return (int)false;
}

int clib_stdbool_sizeof_bool(void) {
  /* Freestanding bool == _Bool, so sizeof must be 1 on every platform
   * SecureOS targets. Pinning here catches a drift to `int`-aliased
   * `bool` (which would silently change sizeof and pointer aliasing). */
  return (int)sizeof(bool);
}

int clib_stdbool_feature_macro_value(void) {
  return (int)__bool_true_false_are_defined;
}
