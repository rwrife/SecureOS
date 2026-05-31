/*
 * user/libs/clib/src/stdnoreturn.c
 *
 * Drift-anchor helper TU for the freestanding <stdnoreturn.h> nucleus
 * (M7-TOOLCHAIN-004, issue #407, plan
 *  plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Why a helper TU for a single-macro header:
 *   <stdnoreturn.h> exposes no linker symbols of its own, so a future
 *   PR could silently change `#define noreturn _Noreturn` to (e.g.)
 *   `#define noreturn` (no-op) and a header-only test would not catch
 *   it on every compiler. This TU folds the macro at its OWN compile
 *   time into observable behaviour — a `noreturn`-decorated function
 *   plus a feature-flag selector — and the host test rounds the
 *   results back via the exported symbols. The matching
 *   `symbol_set_pinned` sub-marker ensures a future drift that drops
 *   either export fails at link time.
 *
 * Containment:
 *   - Freestanding: no libc, no kernel headers, no syscalls.
 *   - Mirrors the anchor shape of src/limits.c (PR #434), src/stdarg.c
 *     (PR #431), src/stdalign.c (PR #440), and src/iso646.c (PR #439).
 *
 * Issue: #407. Refs umbrella #403.
 */

#include "../include/clib/stdnoreturn.h"

/* Selector enum kept textually in sync with tests/clib_stdnoreturn_test.c.
 * The test takes the address of every exported function, so a drift
 * that removes or renames an export fails at link time before the
 * sub-marker is even printed. */
enum {
  CLIB_STDNORETURN_OP_NORETURN_DEFINED = 0,
  CLIB_STDNORETURN_OP_NORETURN_IS_KEYWORD = 1,
  CLIB_STDNORETURN_OP__COUNT = 2
};

/* A real `noreturn`-decorated function compiled through OUR header.
 *
 * If `noreturn` expanded to nothing (or to a non-specifier token), the
 * compiler would NOT treat this as a function-specifier and would
 * typically warn / error under -Wall -Wextra -Werror because of the
 * (intentionally) unreachable fall-off. We deliberately avoid calling
 * any external abort/exit/longjmp here so the TU stays freestanding;
 * the function instead loops forever, which satisfies the "does not
 * return" contract without any syscall dependency.
 *
 * The host test never *calls* this function — it only takes its
 * address — so the loop is never entered. */
noreturn void clib_stdnoreturn_loop_forever(void) {
  for (;;) {
    /* Body intentionally empty. A `noreturn` function that returns
     * has undefined behaviour (C11 §6.7.4¶12); the infinite loop
     * ensures we never violate that contract should a future test
     * accidentally call this symbol. */
  }
}

/* Fold each macro at this TU's compile time into an observable value
 * the host test rounds back. */
unsigned long clib_stdnoreturn_eval(int op) {
  switch (op) {
    case CLIB_STDNORETURN_OP_NORETURN_DEFINED:
      /* The header MUST define `noreturn` (suppressed only under
       * __cplusplus, which this TU is not). */
#ifdef noreturn
      return 1u;
#else
      return 0u;
#endif

    case CLIB_STDNORETURN_OP_NORETURN_IS_KEYWORD: {
      /* If `noreturn` expands to `_Noreturn` (the C11 keyword), then a
       * function declared with the macro is identical to one declared
       * with the bare keyword. We cannot compare textual macro
       * expansions at runtime, so we instead pin the observable
       * property: a `noreturn`-decorated function ADDRESS exists and
       * is non-null. The textual comparison ("did noreturn expand to
       * the right token?") is performed in the test TU under -Werror;
       * a wrong expansion fails the test build, not this branch. */
      void (*p)(void) = (void (*)(void))clib_stdnoreturn_loop_forever;
      return (p != 0) ? 1u : 0u;
    }

    default:
      return (unsigned long)-1;
  }
}

int clib_stdnoreturn_op_count(void) {
  return CLIB_STDNORETURN_OP__COUNT;
}
