/**
 * @file abi_version_test.c
 * @brief Asserts OS_ABI_VERSION header constant matches runtime stub.
 *
 * Purpose:
 *   Implements the "stale stub" guard required by issue #150 and
 *   BUILD_ROADMAP.md §7. If a future change bumps OS_ABI_VERSION in
 *   secureos_abi.h without updating the runtime accessor (or vice
 *   versa), this test fails the build before that drift reaches users.
 *
 * Interactions:
 *   - user/include/secureos_abi.h: source of OS_ABI_VERSION constants.
 *   - user/include/secureos_api.h: declares os_get_abi_version().
 *   - user/runtime/secureos_api_stubs.c: provides the runtime stub.
 *
 * Launched by:
 *   build/scripts/test_abi_version.sh, invoked via
 *   build/scripts/test.sh abi_version and registered in
 *   build/scripts/validate_bundle.sh TEST_TARGETS.
 */

#include "secureos_abi.h"
#include "secureos_api.h"

#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

#define EXPECT_EQ_U(actual, expected, label)                                 \
  do {                                                                       \
    unsigned int _a = (unsigned int)(actual);                                \
    unsigned int _e = (unsigned int)(expected);                              \
    if (_a != _e) {                                                          \
      fprintf(stderr,                                                        \
              "FAIL: %s: expected 0x%08x, got 0x%08x\n",                     \
              (label), _e, _a);                                              \
      failures++;                                                            \
    }                                                                        \
  } while (0)

int main(void) {
  /* Layout sanity: major must occupy bits 16..31, minor bits 0..15. */
  EXPECT_EQ_U(OS_ABI_VERSION_MAJOR_OF(OS_ABI_VERSION),
              OS_ABI_VERSION_MAJOR,
              "major-of(packed) == OS_ABI_VERSION_MAJOR");
  EXPECT_EQ_U(OS_ABI_VERSION_MINOR_OF(OS_ABI_VERSION),
              OS_ABI_VERSION_MINOR,
              "minor-of(packed) == OS_ABI_VERSION_MINOR");

  /* Roadmap §7: must remain 0 until SDK beta. */
  EXPECT_EQ_U(OS_ABI_VERSION_MAJOR, 0u,
              "OS_ABI_VERSION_MAJOR pinned to 0 pre-SDK-beta");

  /* Stale-stub guard: header constant must match runtime accessor. */
  EXPECT_EQ_U(os_get_abi_version(), OS_ABI_VERSION,
              "os_get_abi_version() == OS_ABI_VERSION");

  if (failures != 0) {
    fprintf(stderr, "abi_version_test: %d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  printf("abi_version_test: ok (OS_ABI_VERSION=0x%08x)\n",
         (unsigned int)OS_ABI_VERSION);
  return EXIT_SUCCESS;
}
