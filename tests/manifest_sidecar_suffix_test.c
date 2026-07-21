/**
 * @file tests/manifest_sidecar_suffix_test.c
 * @brief Host contract pin for manifest sidecar suffix macro (issue #580).
 *
 * Confirms that the public libmanifestgen header exports
 * MANIFEST_SIDECAR_SUFFIX with the canonical `.manifest.json` literal and
 * that appending it to a binary path preserves the full binary identity
 * (`<binary>.manifest.json`).
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
  static const char *kExpected = ".manifest.json";
  static const char *kBinary = "/apps/hello.bin";
  char sidecar[256];

  CHECK(strcmp(MANIFEST_SIDECAR_SUFFIX, kExpected) == 0, "header_literal_mismatch");
  fprintf(stdout, "TEST:PASS:manifest_sidecar_suffix:header_literal\n");

  if (snprintf(sidecar, sizeof(sidecar), "%s%s", kBinary, MANIFEST_SIDECAR_SUFFIX) <= 0) {
    fprintf(stderr, "TEST:FAIL:manifest_sidecar_suffix:snprintf_failed\n");
    return 1;
  }
  CHECK(strcmp(sidecar, "/apps/hello.bin.manifest.json") == 0,
        "append_contract_mismatch");
  fprintf(stdout, "TEST:PASS:manifest_sidecar_suffix:append_contract\n");

  if (g_fail) {
    return 1;
  }
  fprintf(stdout, "TEST:PASS:manifest_sidecar_suffix\n");
  return 0;
}
