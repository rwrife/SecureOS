/**
 * @file include/clib/stdbool.h
 * @brief Freestanding <stdbool.h> nucleus for user/libs/clib
 *        (M7-TOOLCHAIN-004 slice 9, issue #407).
 *
 * Purpose:
 *   C11 §4¶6 lists `<stdbool.h>` among the headers a *freestanding*
 *   implementation must provide. TinyCC (#408) and several pending
 *   #407 sibling slices (`stdlib`, `errno`, `bsearch`) already use
 *   plain `int` flags only because no `bool` was available; landing
 *   the canonical macros lets the in-OS toolchain compile any
 *   third-party source that spells `bool` / `true` / `false`.
 *
 * Containment:
 *   - Freestanding. No libc, no kernel includes, no syscalls.
 *   - Pure macro surface plus a tiny drift-anchor helper TU
 *     (`src/stdbool.c`, see `clib_stdbool_*`). No linker symbol can
 *     be pinned for macros directly; mirrors the `<limits.h>` slice
 *     (PR #434) and `<stdarg.h>` slice (PR #431) anchor shape.
 *
 * Symbol / macro coverage (slice 9):
 *   - `bool`  — alias for `_Bool` (C99/C11 keyword).
 *   - `true`  — integer constant 1.
 *   - `false` — integer constant 0.
 *   - `__bool_true_false_are_defined` — integer constant 1 (C11 §7.18).
 *
 * Out of scope for this slice (gated on later #407 slices / consumers):
 *   - stdio (`fopen` / `fprintf` / ...) — needs M7-TOOLCHAIN-002.
 *   - setjmp / longjmp — arch-specific, on-target build.
 *
 * ABI status:
 *   Userland-only freestanding header. Does **not** bump
 *   `OS_ABI_VERSION` (parity with the other #407 slices: `clib_string`,
 *   `clib_ctype`, `clib_qsort`, `clib_limits`, `clib_stdarg`).
 *
 * Issue: #407. Refs umbrella #403. Plan: P3 in
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md`.
 */

#ifndef SECUREOS_USER_LIBS_CLIB_STDBOOL_H
#define SECUREOS_USER_LIBS_CLIB_STDBOOL_H

/* C99 added `_Bool` as a keyword; every compiler SecureOS supports
 * (gcc, clang, TinyCC) provides it natively. We therefore alias `bool`
 * to `_Bool` rather than to `int`, so `sizeof(bool) == 1` and so a
 * pointer-to-bool is not silently aliasable to pointer-to-int.
 *
 * If `__cplusplus` is in scope, `bool` / `true` / `false` are language
 * keywords already; the C11 standard explicitly says the macros may
 * not be defined in that case (§7.18¶4). Guard accordingly. */
#ifndef __cplusplus

#ifndef bool
#define bool _Bool
#endif

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

#endif /* !__cplusplus */

/* C11 §7.18¶3: macro defined as integer constant 1. Even in C++ mode
 * the standard wording does not exclude this one (it only excludes
 * `bool` / `true` / `false`). Define unconditionally so a TU that
 * tests for the feature gets the canonical answer. */
#ifndef __bool_true_false_are_defined
#define __bool_true_false_are_defined 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- Drift-anchor helper TU --------------------------------------------
 *
 * Macros do not produce linker symbols, so `symbol_set_pinned` cannot
 * anchor them directly. The two helpers below fold the macro values
 * at `src/stdbool.c`'s compile time and return them through callable
 * functions; the host test round-trips through them so a future drift
 * in this header is caught against the value the linked `libclib`
 * actually ships, not just what the test TU happens to include.
 *
 * Same shape as `clib_limits_*` (PR #434) and `clib_stdarg_*`
 * (PR #431). */

/** Returns `(int)true` as folded at src/stdbool.c compile time. */
int clib_stdbool_true_value(void);

/** Returns `(int)false` as folded at src/stdbool.c compile time. */
int clib_stdbool_false_value(void);

/** Returns `(int)sizeof(bool)` as folded at src/stdbool.c compile time. */
int clib_stdbool_sizeof_bool(void);

/** Returns `__bool_true_false_are_defined` as folded at src/stdbool.c. */
int clib_stdbool_feature_macro_value(void);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_STDBOOL_H */
