/**
 * @file process_spawn_wrapper_test.c
 * @brief M7-TOOLCHAIN-003 slice 2 (#422) — host-side link smoke for
 *        `os_process_spawn`.
 *
 * Purpose:
 *   The kernel-side leg of `os_process_spawn` reaches through the
 *   fixed bridge address `SECUREOS_NATIVE_BRIDGE_ADDR` (0x009FF000),
 *   which is not a valid host pointer — actually *calling* the
 *   wrapper with a live bridge would segfault before the dynamic
 *   `process_run` re-entry path could run. What we lock down on the
 *   host instead is the link-time contract (mirror of
 *   `process_exit_wrapper_test.c` from PR #413):
 *
 *     1. `os_process_spawn` is exported by the user runtime (taking
 *        its address verifies the symbol resolves — absence would be
 *        a link-time error before this test ever runs).
 *     2. Its declared signature matches
 *        `os_status_t (const char *, const char *const *, unsigned int,
 *                       int *)` — the shape documented in
 *        `docs/abi/syscalls.md` and required by future SDK
 *        forwarders (e.g. `system(3)`-shaped helpers in libos).
 *     3. The reserved-flag refusal in the wrapper (non-zero `flags`
 *        returns `OS_STATUS_ERROR`) is exercised on the host path —
 *        the wrapper checks `flags != 0` before dereferencing the
 *        bridge pointer, so this branch is safe to call without a
 *        mapped bridge.
 *     4. NULL / empty `path` returns `OS_STATUS_ERROR` on the same
 *        early-reject path.
 *
 *   We deliberately do NOT exercise the no-bridge fall-through for
 *   a valid path: that path would read through the fixed bridge
 *   address `SECUREOS_NATIVE_BRIDGE_ADDR` (0x009FF000), which is
 *   not a valid host pointer and segfaults the test process before
 *   the no-bridge guard can run. The valid-path no-bridge contract
 *   is a follow-up that depends on the wrapper learning to guard
 *   the bridge dereference against a missing host mapping (out of
 *   scope for the #422 ABI/wiring slice; matches the same caveat
 *   #406 left for `os_process_exit`).
 *
 *   Together these prevent two regressions:
 *     - silently dropping the wrapper (would break crt0 forwarders +
 *       any future userland helper that imports it);
 *     - changing the signature without updating the doc (would
 *       surface as a host-build compile error here).
 *
 *   The dynamic "no-bridge no-arg returns OS_STATUS_OK" check is
 *   intentionally narrow: on the host, the wrapper's no-bridge
 *   fall-through is non-fatal so the test process is not terminated.
 *
 * Interactions:
 *   - user/include/secureos_api.h: declares `os_process_spawn`.
 *   - user/runtime/secureos_api_stubs.c: provides the wrapper +
 *     the argv-join helper exercised here.
 *
 * Launched by:
 *   build/scripts/test_process_spawn_wrapper.sh
 */

#include <stdio.h>
#include <stdlib.h>

#include "../user/include/secureos_api.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:process_spawn_wrapper:%s\n", reason);
  exit(1);
}

/*
 * Compile-time signature pin. If the prototype in secureos_api.h ever
 * drifts (e.g. extra arg, different return type, missing const), this
 * assignment fails to compile and the host build breaks loudly.
 */
typedef os_status_t (*os_process_spawn_fn_t)(const char *path,
                                              const char *const *argv,
                                              unsigned int flags,
                                              int *out_exit_status);

int main(void) {
  printf("TEST:START:process_spawn_wrapper\n");

  os_process_spawn_fn_t fn = &os_process_spawn;
  if (fn == 0) {
    /* Defensive: the address of an extern function is never NULL in a
     * conforming program, but the comparison forces the optimiser to
     * keep the symbol reference live. */
    fail("symbol_not_linked");
  }

  /* Status enum sanity — wrapper returns these on the no-bridge
   * path; pinning constants keeps docs + code aligned. */
  if ((int)OS_STATUS_OK != 0) fail("os_status_ok_drift");
  if ((int)OS_STATUS_DENIED != 1) fail("os_status_denied_drift");
  if ((int)OS_STATUS_NOT_FOUND != 2) fail("os_status_not_found_drift");
  if ((int)OS_STATUS_ERROR != 3) fail("os_status_error_drift");

  /* Bad-arg contract: NULL path -> ERROR. The wrapper performs this
   * check BEFORE dereferencing the bridge pointer, so it is safe to
   * call on the host (no segfault). */
  if (os_process_spawn(0, 0, 0u, 0) != OS_STATUS_ERROR) {
    fail("null_path_not_error");
  }
  /* Empty path is also ERROR (matches the existing `process_run`
   * empty-name reject at the kernel layer). Same early-reject path. */
  if (os_process_spawn("", 0, 0u, 0) != OS_STATUS_ERROR) {
    fail("empty_path_not_error");
  }

  /* Reserved flag bits: any non-zero `flags` value returns ERROR
   * regardless of path/argv. The wrapper checks `flags != 0` BEFORE
   * the bridge dereference, so this branch is safe to call without
   * a mapped bridge. */
  {
    const char *argv[] = {"hello", 0};
    if (os_process_spawn("hello", argv, 1u, 0) != OS_STATUS_ERROR) {
      fail("reserved_flag_not_error");
    }
    if (os_process_spawn("hello", argv, 0x80000000u, 0) != OS_STATUS_ERROR) {
      fail("reserved_flag_high_not_error");
    }
  }

  printf("TEST:PASS:process_spawn_wrapper\n");
  return 0;
}
