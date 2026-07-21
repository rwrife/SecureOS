/**
 * @file tests/manifestgen/default_arena_bytes_test.c
 * @brief Host gate for issue #595: pin the default `runtime.arena_bytes`
 *        emitted by `libmanifestgen`.
 *
 * This test synthesises a manifest with the standard happy-path params and
 * asserts the emitted JSON includes exactly the compile-time pinned default
 * from `MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES`.
 *
 * Launched by:
 *   build/scripts/test_manifestgen_default_arena.sh
 */

#include <stdio.h>
#include <string.h>

#include "../../user/libs/manifestgen/include/manifestgen/manifest_default.h"

int main(void) {
  manifest_default_params_t params;
  manifest_default_result_t rc;
  char out[2048];
  char needle[64];
  size_t out_len = 0u;
  const char *hit = NULL;

  memset(&params, 0, sizeof(params));
  params.abi_version = 0u;
  params.owner_kind = MANIFEST_OWNER_KIND_LOCAL;
  params.app_id = "helloapp";
  params.app_version = "0.1.0";
  params.subject_id = 2u;
  params.binary_path = "apps/helloapp.bin";

  rc = manifest_default_synthesise(&params, out, sizeof(out), &out_len);
  if (rc != MANIFEST_DEFAULT_OK) {
    fprintf(stderr,
            "TEST:FAIL:manifestgen_default_arena_bytes:synth_rc=%d\n",
            (int)rc);
    return 1;
  }

  (void)snprintf(needle,
                 sizeof(needle),
                 "\"arena_bytes\": %u",
                 (unsigned)MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES);

  hit = strstr(out, needle);
  if (hit == NULL) {
    fprintf(stderr,
            "TEST:FAIL:manifestgen_default_arena_bytes:missing_expected_value:%s\n",
            needle);
    return 1;
  }

  /* Ensure the key is present and the emitted value is not accidentally
   * rewritten to a different integer elsewhere in the output. */
  if (strstr(hit + 1, "\"arena_bytes\":") != NULL) {
    fprintf(stderr,
            "TEST:FAIL:manifestgen_default_arena_bytes:multiple_arena_bytes_entries\n");
    return 1;
  }

  printf("TEST:PASS:manifestgen_default_arena_bytes:emits_pinned_default\n");
  printf("TEST:PASS:manifestgen_default_arena_bytes\n");
  return 0;
}
