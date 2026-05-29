/**
 * @file process_exit_wrapper_test.c
 * @brief M7-TOOLCHAIN-003 (#406) — host-side link smoke for
 *        `os_process_exit`.
 *
 * Purpose:
 *   The kernel-side leg of `os_process_exit` longjmps out of the
 *   running entry() call via the launcher's fault-recovery slot;
 *   that path cannot be exercised on the host. The user-runtime
 *   wrapper in `user/runtime/secureos_api_stubs.c` reaches through
 *   the fixed bridge address `SECUREOS_NATIVE_BRIDGE_ADDR`
 *   (0x009FF000), which is not a valid host pointer — actually
 *   *calling* the wrapper here would segfault before reaching the
 *   no-bridge fall-through path.
 *
 *   What we lock down on the host instead is the link-time contract:
 *
 *     1. `os_process_exit` is exported by the user runtime (taking
 *        its address verifies the symbol resolves — absence would
 *        be a link-time error before this test ever runs).
 *     2. Its declared signature matches `os_status_t (int)` — the
 *        shape documented in `docs/abi/syscalls.md` and required by
 *        `sdk/lib/crt0.c`'s `_os_exit` forwarder.
 *
 *   Together these prevent two regressions:
 *     - silently dropping the wrapper (would also break crt0's
 *       `_os_exit`, which now forwards through it);
 *     - changing the signature without updating the doc / crt0
 *       (would surface as a host-build compile error here).
 *
 *   The dynamic "no-bridge returns OS_STATUS_OK" check is
 *   intentionally deferred: it can only be exercised once the
 *   wrapper learns to guard the bridge dereference against a
 *   missing host mapping (out of scope for the M7-TOOLCHAIN-003
 *   ABI/wiring slice).
 *
 * Interactions:
 *   - user/include/secureos_api.h: declares `os_process_exit`.
 *   - user/runtime/secureos_api_stubs.c: provides the wrapper we
 *     are taking the address of.
 *   - sdk/lib/crt0.c: forwards `_os_exit(status)` through this
 *     wrapper; the signature pin here is what keeps that
 *     forwarder compiling.
 *
 * Launched by:
 *   build/scripts/test_process_exit_wrapper.sh
 */

#include <stdio.h>
#include <stdlib.h>

#include "../user/include/secureos_api.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:process_exit_wrapper:%s\n", reason);
  exit(1);
}

/*
 * Compile-time signature pin. If the prototype in secureos_api.h ever
 * drifts (e.g. an extra arg, or `void` return), this assignment fails
 * to compile and the host build breaks loudly.
 */
typedef os_status_t (*os_process_exit_fn_t)(int status);

int main(void) {
  printf("TEST:START:process_exit_wrapper\n");

  os_process_exit_fn_t fn = &os_process_exit;
  if (fn == 0) {
    /* Defensive: the address of an extern function is never NULL in a
     * conforming program, but the comparison forces the optimiser to
     * keep the symbol reference live. */
    fail("symbol_not_linked");
  }

  /* Status enum sanity — the wrapper returns this on the no-bridge
   * path in a future iteration; pinning the constant keeps docs +
   * code aligned. */
  if ((int)OS_STATUS_OK != 0) {
    fail("os_status_ok_drift");
  }

  printf("TEST:PASS:process_exit_wrapper\n");
  return 0;
}
