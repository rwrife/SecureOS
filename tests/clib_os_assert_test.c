/*
 * tests/clib_os_assert_test.c
 *
 * Host link-pin + no-bridge branch coverage for the `clib_os_assert`
 * forwarder (M7-TOOLCHAIN-004 follow-up, issue #407; on-target
 * sibling of the freestanding `<clib/assert.h>` nucleus shipped by
 * PR #443).
 *
 * Purpose:
 *   The on-target assert-failure path forwards through
 *   `os_process_exit(1)`, which on a kernel-hosted run does not
 *   return. On the host build there is no native bridge mapped, so
 *   `secureos_api_stubs.c` returns `OS_STATUS_OK` instead of
 *   terminating the test process. The forwarder must therefore
 *   never return into its caller on either path; on the host that
 *   means it falls through to a tight loop — same shape the
 *   freestanding default handler in `src/assert.c` uses.
 *
 *   What we lock down on the host:
 *
 *     1. `clib_os_assert_forwarder` is exported with the exact
 *        `clib_assert_handler_fn` signature, so it can be passed
 *        straight to `clib_assert_set_handler(...)` without a
 *        trampoline. A compile-time pin via a function-pointer
 *        assignment fails the build loudly if the signature ever
 *        drifts (mirrors `clib_os_brk_test`'s `g_forwarder_pin`).
 *
 *     2. `clib_os_assert_install()` is reachable as an extern
 *        symbol — the convenience installer SDK apps will call at
 *        startup. We do NOT actually install it in this test (that
 *        would replace the test's longjmp-shaped handler), but a
 *        compile-time pin via function-pointer assignment forces
 *        the linker to keep the symbol live.
 *
 *   The actual "forwarder calls os_process_exit and does not return"
 *   behaviour is unreachable from a bare host test (calling it would
 *   either kill the test process if a bridge ever shows up, or spin
 *   forever in the fall-through loop). The live exit round-trip is
 *   the deferred on-target peer, analogous to the
 *   `clib_brk_growth_qemu` peer #421 calls out for `clib_os_brk`.
 *
 * Interactions:
 *   - user/libs/clib/include/clib/assert.h
 *   - user/libs/clib/include/clib/os_assert.h
 *   - user/include/secureos_api.h
 *   - user/runtime/secureos_api_stubs.c
 *
 * Launched by:
 *   build/scripts/test_clib_os_assert.sh
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "../user/libs/clib/include/clib/assert.h"
#include "../user/libs/clib/include/clib/os_assert.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:clib_os_assert:%s\n", reason);
  exit(1);
}

/*
 * Compile-time signature pin. If the prototype in
 * `clib/os_assert.h` ever drifts away from the
 * `clib_assert_handler_fn` shape in `clib/assert.h`, this
 * assignment fails to compile and the host build breaks loudly.
 */
static clib_assert_handler_fn g_forwarder_pin = &clib_os_assert_forwarder;

/*
 * Compile-time pin for the installer convenience. Keeps the symbol
 * live in the binary and asserts its `void(void)` signature.
 */
typedef void (*clib_os_assert_install_fn)(void);
static clib_os_assert_install_fn g_install_pin = &clib_os_assert_install;

int main(void) {
  printf("TEST:START:clib_os_assert\n");

  if (g_forwarder_pin == 0) {
    /* Defensive: the address of an extern function is never NULL in
     * a conforming program; this comparison forces the optimiser to
     * keep the symbol reference live. */
    fail("forwarder_not_linked");
  }
  printf("TEST:PASS:clib_os_assert:forwarder_signature_pinned\n");

  if (g_install_pin == 0) {
    fail("install_not_linked");
  }
  printf("TEST:PASS:clib_os_assert:install_symbol_pinned\n");

  /*
   * `clib_assert_set_handler` accepts the forwarder without complaint
   * (this is a no-op call from the test's perspective — we don't
   * trigger an assertion afterwards, so the handler is never
   * invoked, and we immediately restore the default by passing 0).
   * The point of the call is to lock down at link time that the
   * setter and the forwarder agree on the function-pointer type.
   */
  clib_assert_set_handler(clib_os_assert_forwarder);
  clib_assert_set_handler(0);
  printf("TEST:PASS:clib_os_assert:setter_accepts_forwarder\n");

  printf("TEST:PASS:clib_os_assert\n");
  return 0;
}
