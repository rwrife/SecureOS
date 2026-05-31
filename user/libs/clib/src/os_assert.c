/*
 * user/libs/clib/src/os_assert.c
 *
 * On-target assert-handler forwarder that calls `os_process_exit(1)`
 * (M7-TOOLCHAIN-004 follow-up, issue #407; depends on #406 / PR #427
 * for the `os_process_exit` syscall and `secureos_api_stubs.c`
 * runtime wrapper).
 *
 * Containment posture matches `clib_os_brk` (#421 slice 3, PR #455):
 * the freestanding nucleus (`src/assert.c`) stays syscall-free, and
 * this sibling TU is the only place the bridge is reached for the
 * assert path. SDK apps opt in by `#include "clib/os_assert.h"` and
 * a single `clib_os_assert_install()` call at startup.
 *
 * The implementation deliberately makes no assumption about whether
 * the bridge call returns — the `os_process_exit` contract is
 * "does not return on success" only when the bridge is attached. In
 * the host no-bridge case `secureos_api_stubs.c` returns
 * `OS_STATUS_OK` so the test process is not terminated, which is
 * why we fall through to a tight loop after the call (mirrors the
 * default handler in `src/assert.c`). Returning into the caller
 * would let the failing assert's `else` branch execute as if the
 * check had passed.
 */

#include "../include/clib/os_assert.h"
#include "../../../include/secureos_api.h"

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Noreturn
#endif
void clib_os_assert_forwarder(const char *expr,
                              const char *file,
                              int         line,
                              const char *func) {
  /* Args are intentionally unused: the freestanding nucleus has
   * already done whatever logging the embedder wired (none today;
   * stdio integration is a later #407 slice). This forwarder's only
   * job is to terminate the process with a non-zero status. */
  (void)expr;
  (void)file;
  (void)line;
  (void)func;

  (void)os_process_exit(1);

  /* Bridge-absent fall-through (host build with no bridge mapped).
   * `os_process_exit` returned `OS_STATUS_OK` rather than killing the
   * process. We must NOT return into `__clib_assert_fail`'s caller,
   * so spin — same shape as the default handler. */
  for (;;) {
  }
}

void clib_os_assert_install(void) {
  clib_assert_set_handler(clib_os_assert_forwarder);
}
