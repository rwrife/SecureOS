/*
 * user/libs/clib/src/assert.c
 *
 * Freestanding `<assert.h>` nucleus translation unit
 * (issue #407 / M7-TOOLCHAIN-004, plan
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3).
 *
 * Implements the registered-handler hook described in
 * `include/clib/assert.h`. No syscall dependency, no allocator
 * dependency, no globals other than the handler pointer itself.
 *
 * Default handler when none is installed:
 *   - Tight `for (;;) {}` loop (the same shape `sdk/lib/crt0.c`'s
 *     `_os_exit` falls through to on a bare-metal abort). Never
 *     returns; never traps to the kernel directly (that's the
 *     on-target forwarder's job once #406 lands `os_process_exit`).
 *
 * The handler-pointer load/store is plain assignment — no atomics,
 * no `volatile`. SecureOS userland is single-threaded today; if
 * threads ever land we'll revisit (this is the same posture every
 * sibling slice takes for its hook surface, e.g. `clib_brk_fn`).
 */

#include "../include/clib/assert.h"

static clib_assert_handler_fn g_clib_assert_handler = 0;

void clib_assert_set_handler(clib_assert_handler_fn h) {
  g_clib_assert_handler = h;
}

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Noreturn
#endif
void __clib_assert_fail(const char *expr,
                        const char *file,
                        int         line,
                        const char *func) {
  clib_assert_handler_fn h = g_clib_assert_handler;
  if (h) {
    h(expr, file, line, func);
    /* Contract: handlers MUST NOT return. If one does, fall through
     * to the default loop rather than returning into the caller (which
     * would let the failing expression's `else` branch execute as if
     * the assert had passed). */
  }
  for (;;) {
    /* Spin. The host test installs a longjmp-shaped handler, so this
     * loop is unreachable under the test. The on-target build will
     * install a forwarder to `os_process_exit(1)` once #406 lands. */
  }
}
