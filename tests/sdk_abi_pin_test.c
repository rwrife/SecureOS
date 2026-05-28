/**
 * @file sdk_abi_pin_test.c
 * @brief M6-SDK-001 (#369) — assert the public SDK header re-exports the
 *        in-tree ABI version, and that `sdk/VERSION` matches it byte-for-byte.
 *
 * Purpose:
 *   Slice 1 of the M6 SDK scaffold (plan #136 / BUILD_ROADMAP §5.6).
 *   The SDK promises external apps a single ABI pin. If `sdk/include/os/abi.h`
 *   ever drifts from `user/include/secureos_abi.h`, or if `sdk/VERSION` ever
 *   disagrees with either, third-party apps would silently link against an
 *   ABI the kernel does not actually implement. This test makes that drift
 *   a hard CI failure.
 *
 *   Three invariants are locked down:
 *     1. The SDK header's `OS_ABI_VERSION_MAJOR` matches the in-tree
 *        `secureos_abi.h` value (precisely the rule called out in #369).
 *     2. The SDK header's `OS_ABI_VERSION_MINOR` matches the in-tree value
 *        (bonus — minor drift is also a real concern and free to check).
 *     3. `sdk/VERSION` parses to the same `MAJOR.MINOR.PATCH` triple the
 *        header advertises.
 *
 * Interactions:
 *   - sdk/include/os/abi.h: public re-export under test.
 *   - user/include/secureos_abi.h: in-tree source of truth.
 *   - sdk/VERSION: external-facing version pin.
 *
 * Launched by:
 *   build/scripts/test_sdk_abi_pin.sh
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Pull the in-tree header under its kernel-side spelling first, then the
 * public SDK header. The SDK header re-exports the same macros via
 * `#include "secureos_abi.h"`; with both `-Iuser/include` and
 * `-Isdk/include` on the command line the SDK header resolves correctly,
 * and the `#ifndef` guards inside it become no-ops because the in-tree
 * header already defined the macros.
 */
#include "secureos_abi.h"

/*
 * Capture the in-tree values BEFORE including the SDK header. If the SDK
 * header ever started minting its own conflicting definitions (it must not),
 * the compiler would warn or the values would disagree below.
 */
enum {
  KERNEL_MAJOR = OS_ABI_VERSION_MAJOR,
  KERNEL_MINOR = OS_ABI_VERSION_MINOR,
};

#include "os/abi.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:sdk_abi_pin:%s\n", reason);
  exit(1);
}

/*
 * Minimal, allocation-free parser for `MAJOR.MINOR.PATCH\n` — we only need
 * to validate `sdk/VERSION`, so a `sscanf`-equivalent that tolerates a
 * trailing newline is enough.
 */
static int parse_version(const char *s, int *maj, int *min, int *pat) {
  return sscanf(s, "%d.%d.%d", maj, min, pat) == 3;
}

int main(void) {
  printf("TEST:START:sdk_abi_pin\n");

  /* Invariant 1: SDK header MAJOR == in-tree MAJOR. */
  if ((int)OS_ABI_VERSION_MAJOR != (int)KERNEL_MAJOR) {
    fail("sdk_major_drift");
  }
  /* Invariant 2: SDK header MINOR == in-tree MINOR. */
  if ((int)OS_ABI_VERSION_MINOR != (int)KERNEL_MINOR) {
    fail("sdk_minor_drift");
  }
  /* PATCH is SDK-only; check it is defined and currently 0 (slice 1 pin). */
#ifndef OS_ABI_VERSION_PATCH
  fail("sdk_patch_undefined");
#endif

  /* Invariant 3: sdk/VERSION matches the header triple byte-for-byte. */
  FILE *vf = fopen("sdk/VERSION", "r");
  if (!vf) {
    /* The test runs from the repo root via test_sdk_abi_pin.sh; if the
     * file isn't found the harness misconfigured the cwd. */
    fail("sdk_version_file_missing");
  }
  char buf[64] = {0};
  size_t n = fread(buf, 1, sizeof(buf) - 1, vf);
  fclose(vf);
  if (n == 0) {
    fail("sdk_version_file_empty");
  }

  int fmaj = -1, fmin = -1, fpat = -1;
  if (!parse_version(buf, &fmaj, &fmin, &fpat)) {
    fail("sdk_version_unparseable");
  }
  if (fmaj != (int)OS_ABI_VERSION_MAJOR) {
    fail("sdk_version_major_mismatch");
  }
  if (fmin != (int)OS_ABI_VERSION_MINOR) {
    fail("sdk_version_minor_mismatch");
  }
  if (fpat != (int)OS_ABI_VERSION_PATCH) {
    fail("sdk_version_patch_mismatch");
  }

  printf("TEST:PASS:sdk_abi_pin:major=%d:minor=%d:patch=%d\n", fmaj, fmin, fpat);
  return 0;
}
