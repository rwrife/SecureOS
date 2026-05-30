/*
 * include/clib/stdnoreturn.h
 * Freestanding userland <stdnoreturn.h> nucleus (M7-TOOLCHAIN-004, issue #407).
 *
 * Purpose:
 *   ISO C11 §4¶6 lists <stdnoreturn.h> among the headers a *freestanding*
 *   implementation must provide. §7.23 defines the header as a single
 *   convenience macro `noreturn` that expands to the `_Noreturn`
 *   function-specifier keyword (C11 §6.7.4).
 *
 *   TinyCC (#408) and several pending #407 sibling slices declare
 *   abort-like helpers (e.g. `clib_panic`, `__assert_fail`) that the
 *   in-OS toolchain consumers spell with `noreturn` rather than the
 *   bare `_Noreturn` keyword. Landing the canonical macro lets the
 *   in-OS toolchain compile any source that spells `noreturn`.
 *
 * Containment:
 *   - Freestanding. No libc, no kernel includes, no syscalls.
 *   - Pure macro surface plus a tiny drift-anchor helper TU
 *     (`src/stdnoreturn.c`, see `clib_stdnoreturn_*`). No linker
 *     symbol can be pinned for macros directly; mirrors the
 *     `<limits.h>` slice (PR #434), `<stdarg.h>` slice (PR #431),
 *     `<stdalign.h>` slice (PR #440), and `<iso646.h>` slice (PR
 *     #439) anchor shape.
 *
 * Symbol / macro coverage (this slice):
 *   - `noreturn` — alias for `_Noreturn` (C11 §7.23¶1).
 *
 * Out of scope for this slice (gated on later #407 slices / consumers):
 *   - stdio (`fopen` / `fprintf` / ...) — needs M7-TOOLCHAIN-002.
 *   - setjmp / longjmp — arch-specific, on-target build.
 *
 * ABI status:
 *   Userland-only freestanding header. Does **not** bump
 *   `OS_ABI_VERSION` (parity with the other #407 slices: `clib_string`,
 *   `clib_ctype`, `clib_qsort`, `clib_limits`, `clib_stdarg`,
 *   `clib_iso646`).
 *
 * Issue: #407. Refs umbrella #403. Plan: P3 in
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md`.
 */

#ifndef SECUREOS_USER_LIBS_CLIB_STDNORETURN_H
#define SECUREOS_USER_LIBS_CLIB_STDNORETURN_H

/* C11 §7.23¶1: the header defines `noreturn` to expand to `_Noreturn`.
 *
 * In C++, `noreturn` is a standard attribute (`[[noreturn]]`) and
 * `_Noreturn` is not a keyword; the C11 macro definition is therefore
 * suppressed under C++ to match the WG14 intent and to avoid colliding
 * with `[[noreturn]]` in headers that surface both. */
#ifndef __cplusplus

#ifndef noreturn
#define noreturn _Noreturn
#endif

#endif /* !__cplusplus */

#endif /* SECUREOS_USER_LIBS_CLIB_STDNORETURN_H */
