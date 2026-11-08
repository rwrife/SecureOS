/**
 * @file tests/manifest_sidecar_suffix_test.c
 * @brief Host contract pin for manifest sidecar suffix macro (issue #580).
 *
 * Confirms that the public libmanifestgen header exports
 * MANIFEST_SIDECAR_SUFFIX with the canonical `.mft` literal and
 * that replacing the .bin extension yields a valid FAT 8.3 sibling.
 */

#include <stdio.h>
#include <string.h>

#include "../user/libs/manifestgen/include/manifestgen/manifest_default.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:manifest_sidecar_suffix:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

int main(void) {
  static const char *kExpected = ".mft";
  static const char *kBinary = "/apps/hello.bin";
  char sidecar[256];

  CHECK(strcmp(MANIFEST_SIDECAR_SUFFIX, kExpected) == 0, "header_literal_mismatch");
  fprintf(stdout, "TEST:PASS:manifest_sidecar_suffix:header_literal\n");

  /* In FAT 8.3, sidecar replaces extension (.bin -> .mft). */
  {
    const char *dot = strrchr(kBinary, '.');
    size_t stem_len = dot ? (size_t)(dot - kBinary) : strlen(kBinary);
    if (snprintf(sidecar, sizeof(sidecar), "%.*s%s", (int)stem_len, kBinary, MANIFEST_SIDECAR_SUFFIX) <= 0) {
      fprintf(stderr, "TEST:FAIL:manifest_sidecar_suffix:snprintf_failed\n");
      return 1;
    }
  }
  CHECK(strcmp(sidecar, "/apps/hello.mft") == 0,
        "append_contract_mismatch");
  fprintf(stdout, "TEST:PASS:manifest_sidecar_suffix:append_contract\n");

  if (g_fail) {
    return 1;
  }
  fprintf(stdout, "TEST:PASS:manifest_sidecar_suffix\n");
  return 0;
}
