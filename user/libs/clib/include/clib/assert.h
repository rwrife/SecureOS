/*
 * user/libs/clib/include/clib/assert.h
 *
 * Freestanding `<assert.h>` nucleus for the in-OS toolchain libc
 * (issue #407 / M7-TOOLCHAIN-004, plan
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3).
 *
 * C11 §7.2 specifies <assert.h> as part of the hosted-required header
 * set. It is technically not in §4¶6's freestanding-required minimum
 * (only `static_assert` is, via <assert.h>'s expansion in C11+), but
 * TinyCC (#408), the bsearch slice (PR #433), the qsort slice
 * (PR #418, merged), and any third-party SDK code consumed by the
 * in-OS toolchain (#403) routinely `#include <assert.h>` and call
 * `assert(expr)`. Shipping it here lets that code link unchanged
 * against `libclib.a` without dragging in a hosted libc.
 *
 * Why a registered-handler hook (not a hard-coded panic)
 * ------------------------------------------------------
 * Other clib slices that need a side effect on failure use a
 * registered callback (see `clib_brk_fn` in `<clib/malloc.h>`) rather
 * than baking in a particular abort path. That keeps the slice
 * host-testable today AND on-target tomorrow:
 *
 *   - Host tests register a handler that records the failure args and
 *     `longjmp`s out so the test can continue and verify them.
 *   - On-target runtime (post-M7-TOOLCHAIN-003, #406) registers a
 *     forwarder that calls `os_process_exit(1)`.
 *   - Default-when-unregistered: tight loop. Never returns (the
 *     handler must not return either; `_Noreturn` is contractual).
 *
 * Contract (matches the canonical ISO C surface):
 *
 *     #ifdef NDEBUG
 *       #define assert(expr) ((void)0)
 *     #else
 *       #define assert(expr) <test, call __clib_assert_fail on fail>
 *     #endif
 *     #define static_assert _Static_assert            (C11 §7.2¶3)
 *
 * Plus the project's extension hook:
 *
 *     typedef void (*clib_assert_handler_fn)(const char *expr,
 *                                            const char *file,
 *                                            int         line,
 *                                            const char *func);
 *     void clib_assert_set_handler(clib_assert_handler_fn h);
 *     _Noreturn void __clib_assert_fail(const char *expr,
 *                                       const char *file,
 *                                       int         line,
 *                                       const char *func);
 *
 * Re-includable. The macro is re-defined on every include so that
 * toggling `NDEBUG` between two `#include <assert.h>` lines flips the
 * macro state — exactly as C11 §7.2¶1 requires.
 *
 * ABI status: userland-only, additive. No `OS_ABI_VERSION` bump
 * (parity with every prior #407 slice).
 */

/* §7.2¶1: re-include semantics — DO NOT use a header guard around the
 * assert macro itself. The handler-hook surface IS guarded, since it
 * is plain function declarations whose redefinition would be a
 * compile error. */

#ifndef CLIB_ASSERT_H_NONMACRO_SURFACE
#define CLIB_ASSERT_H_NONMACRO_SURFACE

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*clib_assert_handler_fn)(const char *expr,
                                       const char *file,
                                       int         line,
                                       const char *func);

/* Install a custom assertion-failure handler. The handler is invoked
 * by `__clib_assert_fail` on a failed `assert()`. It MUST NOT return
 * (typically `longjmp`s in tests or `os_process_exit(1)` on-target).
 * Passing `NULL` restores the default (infinite-loop) handler. */
void clib_assert_set_handler(clib_assert_handler_fn h);

/* Failure entry point. Public so the macro below can call it without
 * a private include. Never returns; if the registered handler ever
 * returns, the implementation falls through to an infinite loop. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Noreturn
#endif
void __clib_assert_fail(const char *expr,
                        const char *file,
                        int         line,
                        const char *func);

/* C11 §7.2¶3: <assert.h> shall also define this feature-test macro. */
#define __assert_is_defined 1

#ifdef __cplusplus
}
#endif

#endif /* CLIB_ASSERT_H_NONMACRO_SURFACE */

/* ---- macro surface, redefined on every include per §7.2¶1 ---- */

#undef assert

#ifdef NDEBUG
#  define assert(expr) ((void)0)
#else
#  define assert(expr)                                                       \
     ((void)((expr)                                                          \
       ? 0                                                                   \
       : (__clib_assert_fail(#expr, __FILE__, __LINE__, __func__), 0)))
#endif

/* C11 §7.2¶3: `static_assert` is a convenience macro for the C11
 * keyword `_Static_assert`. C++11 / C23 already provide it as a
 * keyword; only define here when the C11 keyword is the spelling in
 * play. */
#if !defined(__cplusplus) && \
    defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && \
    !defined(static_assert)
#  define static_assert _Static_assert
#endif
