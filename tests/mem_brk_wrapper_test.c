/**
 * @file mem_brk_wrapper_test.c
 * @brief M7-TOOLCHAIN-001 slice 2 (#421) — host-side link smoke for
 *        `os_mem_brk`.
 *
 * Purpose:
 *   Like `process_exit_wrapper_test.c`, the kernel side of
 *   `os_mem_brk` runs inside the launcher's app context and reaches
 *   through the fixed bridge address `SECUREOS_NATIVE_BRIDGE_ADDR`
 *   (0x009FF000). That address is not a valid host pointer, so the
 *   wrapper itself cannot be dynamically driven on the host without
 *   first stubbing the bridge mapping (deferred — see below).
 *
 *   What we lock down on the host:
 *
 *     1. `os_mem_brk` is exported by the user runtime (taking its
 *        address verifies the symbol resolves — absence would be a
 *        link-time error before this test ever runs).
 *     2. Its declared signature matches `os_status_t (int, void **)` —
 *        the shape documented in `docs/abi/syscalls.md` and required
 *        by the freestanding `user/libs/clib` allocator's
 *        `clib_brk_fn` forwarder.
 *     3. The NULL-out-pointer guard returns `OS_STATUS_ERROR` without
 *        dereferencing the bridge. This is the only branch that can
 *        be exercised without a mapped bridge, and it is what the
 *        clib allocator relies on for the "no-bridge / host" fall-
 *        through path.
 *
 *   Together these prevent two regressions:
 *     - silently dropping the wrapper (would also break clib's brk
 *       forwarder once it lands);
 *     - changing the signature without updating the doc / clib
 *       (would surface as a host-build compile error here).
 *
 *   The dynamic "no-bridge returns OS_STATUS_ERROR" check below
 *   relies on the wrapper short-circuiting before any bridge
 *   dereference when `out_prev_break == NULL`, which is exactly the
 *   contract the runtime stubs implement.
 *
 * Interactions:
 *   - user/include/secureos_api.h: declares `os_mem_brk`.
 *   - user/runtime/secureos_api_stubs.c: provides the wrapper we
 *     are taking the address of.
 *
 * Launched by:
 *   build/scripts/test_mem_brk_wrapper.sh
 */

#include <stdio.h>
#include <stdlib.h>

#include "../user/include/secureos_api.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:mem_brk_wrapper:%s\n", reason);
  exit(1);
}

/*
 * Compile-time signature pin. If the prototype in secureos_api.h ever
 * drifts (e.g. width change on `delta`, removed out-param), this
 * assignment fails to compile and the host build breaks loudly.
 */
typedef os_status_t (*os_mem_brk_fn_t)(int delta, void **out_prev_break);

int main(void) {
  printf("TEST:START:mem_brk_wrapper\n");

  os_mem_brk_fn_t fn = &os_mem_brk;
  if (fn == 0) {
    /* Defensive: the address of an extern function is never NULL in a
     * conforming program, but the comparison forces the optimiser to
     * keep the symbol reference live. */
    fail("symbol_not_linked");
  }

  /* Status enum sanity — the wrapper returns these on the no-bridge
   * / NULL-out paths; pinning the constants keeps docs + code
   * aligned with `clib_brk_fn`'s callers in user/libs/clib. */
  if ((int)OS_STATUS_OK != 0) {
    fail("os_status_ok_drift");
  }
  if ((int)OS_STATUS_DENIED != 1) {
    fail("os_status_denied_drift");
  }
  if ((int)OS_STATUS_ERROR != 3) {
    fail("os_status_error_drift");
  }

  /* NULL-out guard: the wrapper MUST reject this without dereferencing
   * the bridge, otherwise the host process would segfault on the
   * unmapped 0x009FF000 read. Reaching the next printf is itself the
   * pass signal. */
  os_status_t rc = os_mem_brk(0, NULL);
  if (rc != OS_STATUS_ERROR) {
    fail("null_out_not_rejected");
  }

  printf("TEST:PASS:mem_brk_wrapper\n");
  return 0;
}
