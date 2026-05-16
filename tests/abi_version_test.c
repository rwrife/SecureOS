/**
 * @file abi_version_test.c
 * @brief Anchors OS_ABI_VERSION at the value mandated by BUILD_ROADMAP.md §7.
 *
 * Purpose:
 *   Locks down two invariants:
 *     1. The compile-time `OS_ABI_VERSION` packed constant matches the
 *        documented `major.minor` pair (currently 0.0).
 *     2. The runtime accessor `os_get_abi_version()` returns the same
 *        value the header advertises, catching stale stubs if the
 *        constant is ever bumped without re-linking the user runtime.
 *
 * Interactions:
 *   - secureos_abi.h: source of truth for the version macros.
 *   - secureos_api.h: declares `os_get_abi_version()`.
 *   - user/runtime/secureos_api_stubs.c: provides the runtime accessor
 *     implementation.
 *
 * Launched by:
 *   build/scripts/test_abi_version.sh
 */

#include <stdio.h>
#include <stdlib.h>

#include "../user/include/secureos_abi.h"
#include "../user/include/secureos_api.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:abi_version:%s\n", reason);
  exit(1);
}

int main(void) {
  printf("TEST:START:abi_version\n");

  if (OS_ABI_VERSION_MAJOR != 0) {
    fail("major_not_zero");
  }
  if (OS_ABI_VERSION_MINOR != 0) {
    fail("minor_not_zero");
  }

  unsigned int packed = (unsigned int)OS_ABI_VERSION;
  if (packed != ((OS_ABI_VERSION_MAJOR << 16) | OS_ABI_VERSION_MINOR)) {
    fail("packed_layout_mismatch");
  }

  unsigned int runtime = os_get_abi_version();
  if (runtime != packed) {
    fail("runtime_accessor_drift");
  }

  printf("TEST:PASS:abi_version:os_abi_version=0x%08x\n", packed);
  return 0;
}
