/*
 * user/libs/clib/include/clib/os_assert.h
 *
 * On-target forwarder that connects `<clib/assert.h>`'s registered-
 * handler hook to the `os_process_exit` syscall surface
 * (M7-TOOLCHAIN-004 follow-up, issue #407; depends on the
 * M7-TOOLCHAIN-003 `os_process_exit` wiring landed by #406 via PR
 * #427).
 *
 * Why a separate translation unit
 * -------------------------------
 * `user/libs/clib/src/assert.c` is freestanding — no libc, no
 * syscalls, no `secureos_api.h` dependency — so it can be host-tested
 * against a longjmp-shaped handler without pulling the bridge in.
 * The on-target abort path therefore lives here, in a sibling TU
 * that an SDK app opts into by:
 *
 *     #include "clib/os_assert.h"
 *     clib_os_assert_install();
 *
 * This is the same containment shape `clib_os_brk` (#421 slice 3,
 * PR #455) uses to wire `clib_malloc`'s `clib_brk_fn` hook to
 * `os_mem_brk` without contaminating `<clib/malloc.h>` itself.
 *
 * Contract
 * --------
 *   - `clib_os_assert_forwarder` has the exact `clib_assert_handler_fn`
 *     signature, so it can be passed straight to
 *     `clib_assert_set_handler(...)` without a trampoline.
 *   - It calls `os_process_exit(1)`. When the native bridge is
 *     attached (kernel-hosted run), control is surrendered to the
 *     kernel and the call never returns. If the bridge is absent
 *     (host build with no bridge mapped) the wrapper returns
 *     `OS_STATUS_OK`; we then fall through to the same tight loop
 *     `__clib_assert_fail`'s default uses, satisfying the assert
 *     handler's must-not-return contract.
 *   - `clib_os_assert_install()` is a convenience that calls
 *     `clib_assert_set_handler(clib_os_assert_forwarder)`. SDK apps
 *     usually want this and nothing else; embedders that need more
 *     than exit-on-fail can install their own handler instead.
 *
 * ABI status: userland-only, additive. No `OS_ABI_VERSION` bump
 * (parity with every prior #407 slice and with #421 slice 3).
 *
 * Interactions:
 *   - `include/clib/assert.h`        — `clib_assert_handler_fn`
 *                                       typedef + setter.
 *   - `user/include/secureos_api.h`  — `os_process_exit` prototype.
 *   - `src/os_assert.c`              — implementation.
 *   - `tests/clib_os_assert_test.c`  — host link-pin + no-bridge
 *                                       branch coverage.
 *   - `build/scripts/test_clib_os_assert.sh` — driver script.
 */

#ifndef SECUREOS_USER_LIBS_CLIB_OS_ASSERT_H
#define SECUREOS_USER_LIBS_CLIB_OS_ASSERT_H

#include "assert.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Assert handler that forwards to `os_process_exit(1)`. Matches the
 * `clib_assert_handler_fn` signature exactly. Does not return on a
 * kernel-hosted run; falls through to a tight loop on the host
 * no-bridge path so the must-not-return contract still holds.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Noreturn
#endif
void clib_os_assert_forwarder(const char *expr,
                              const char *file,
                              int         line,
                              const char *func);

/*
 * Convenience installer: equivalent to
 * `clib_assert_set_handler(clib_os_assert_forwarder)`. Safe to call
 * multiple times. SDK apps typically call this once during startup.
 */
void clib_os_assert_install(void);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_OS_ASSERT_H */
