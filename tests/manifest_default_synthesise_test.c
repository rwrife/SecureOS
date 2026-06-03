/**
 * @file tests/manifest_default_synthesise_test.c
 * @brief Host unit test for `libmanifestgen` (M7-TOOLCHAIN-006 sub-slice,
 *        issue #533).
 *
 * Covers:
 *   1. Signature pin: synthesise() with happy-path params returns OK,
 *      writes a NUL-terminated buffer, sets out_len to the byte count.
 *   2. Determinism: two synthesises with identical params produce
 *      byte-identical bytes (pins the in-OS `cc` driver's reproducible-
 *      build story).
 *   3. Validator round-trip: the produced JSON is accepted by
 *      `tools/validate_manifests.py` against `manifests/schema/v0.json`
 *      (i.e. is a real v0 manifest, not just an arbitrary JSON blob).
 *      Driven from the .sh peer; this C test only writes the synthesised
 *      bytes to a path the driver passes in via argv[1].
 *   4. Negative: NULL params / NULL out_buf / NULL out_len rejected with
 *      INVALID_ARG; empty required-string / out-of-range subject_id
 *      rejected with INVALID_FIELD.
 *   5. Buffer-too-small: under-sized out_buf returns
 *      ERR_BUFFER_TOO_SMALL with no overrun (canary-byte check past the
 *      cap).
 *   6. `local_kind` arm: synthesise with owner_kind = LOCAL succeeds and
 *      emits `"kind": "local"`. The schema-validator round-trip for this
 *      arm is gated on #522 (additive enum); until #522 lands, the .sh
 *      peer emits a SKIP marker (`:local_kind:awaiting_522`).
 *
 * Launched by:
 *   build/scripts/test_manifest_default_synthesise.sh (dispatched via
 *   build/scripts/test.sh manifest_default_synthesise).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../user/libs/manifestgen/include/manifestgen/manifest_default.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:manifest_default_synthesise:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* Canonical happy-path params used across multiple sub-tests. */
static void fill_default_params(manifest_default_params_t *p) {
  memset(p, 0, sizeof(*p));
  p->abi_version = 0u;                  /* matches OS_ABI_VERSION at v0 */
  p->owner_kind  = MANIFEST_OWNER_KIND_INTERNAL;
  p->app_id      = "helloapp";
  p->app_version = "0.1.0";
  p->subject_id  = 2u;
  p->binary_path = "apps/helloapp.bin";
}

static void test_signature_pin(void) {
  manifest_default_params_t p;
  char  out[1024];
  size_t out_len = 0u;
  manifest_default_result_t rc;

  fill_default_params(&p);

  rc = manifest_default_synthesise(&p, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_OK, "happy_path_ok");
  CHECK(out_len > 0u, "happy_path_nonzero_len");
  /* NUL terminator past the reported length. */
  CHECK(out[out_len] == '\0', "happy_path_nul_terminated");
  CHECK(strlen(out) == out_len, "happy_path_strlen_matches_outlen");
  /* Sanity: contains the required top-level keys (full schema
   * acceptance is the .sh peer's job). */
  CHECK(strstr(out, "\"manifest_version\": 0") != NULL, "happy_path_manifest_version_key");
  CHECK(strstr(out, "\"os_abi_version\": 0") != NULL, "happy_path_os_abi_version_key");
  CHECK(strstr(out, "\"id\": \"helloapp\"") != NULL, "happy_path_app_id_key");
  CHECK(strstr(out, "\"subject_id\": 2") != NULL, "happy_path_subject_id_key");
  CHECK(strstr(out, "\"binary\": \"apps/helloapp.bin\"") != NULL, "happy_path_binary_key");
  CHECK(strstr(out, "\"request\": []") != NULL, "happy_path_caps_empty");
  CHECK(strstr(out, "\"kind\": \"internal\"") != NULL, "happy_path_owner_kind");

  fprintf(stdout, "TEST:PASS:manifest_default_synthesise:happy_path\n");
}

static void test_determinism(void) {
  manifest_default_params_t p;
  char a[1024];
  char b[1024];
  size_t la = 0u;
  size_t lb = 0u;

  fill_default_params(&p);
  manifest_default_result_t ra = manifest_default_synthesise(&p, a, sizeof(a), &la);
  manifest_default_result_t rb = manifest_default_synthesise(&p, b, sizeof(b), &lb);
  CHECK(ra == MANIFEST_DEFAULT_OK && rb == MANIFEST_DEFAULT_OK, "determinism_both_ok");
  CHECK(la == lb, "determinism_lengths_match");
  CHECK(la > 0u && memcmp(a, b, la) == 0, "determinism_bytes_identical");

  fprintf(stdout, "TEST:PASS:manifest_default_synthesise:determinism\n");
}

static void test_negatives(void) {
  manifest_default_params_t p;
  char out[1024];
  size_t out_len = 0u;
  manifest_default_result_t rc;

  fill_default_params(&p);

  /* NULL out_buf rejected. */
  rc = manifest_default_synthesise(&p, NULL, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_ERR_INVALID_ARG, "neg_null_out_buf");

  /* NULL out_len rejected. */
  rc = manifest_default_synthesise(&p, out, sizeof(out), NULL);
  CHECK(rc == MANIFEST_DEFAULT_ERR_INVALID_ARG, "neg_null_out_len");

  /* NULL params rejected. */
  rc = manifest_default_synthesise(NULL, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_ERR_INVALID_ARG, "neg_null_params");

  /* NULL app_id rejected. */
  fill_default_params(&p);
  p.app_id = NULL;
  rc = manifest_default_synthesise(&p, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_ERR_INVALID_ARG, "neg_null_app_id");

  /* Empty app_version rejected. */
  fill_default_params(&p);
  p.app_version = "";
  rc = manifest_default_synthesise(&p, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_ERR_INVALID_FIELD, "neg_empty_app_version");

  /* subject_id = 0 rejected. */
  fill_default_params(&p);
  p.subject_id = 0u;
  rc = manifest_default_synthesise(&p, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_ERR_INVALID_FIELD, "neg_subject_id_zero");

  /* subject_id = 8 rejected (schema cap is 7). */
  fill_default_params(&p);
  p.subject_id = 8u;
  rc = manifest_default_synthesise(&p, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_ERR_INVALID_FIELD, "neg_subject_id_too_high");

  /* Bogus owner_kind rejected. */
  fill_default_params(&p);
  p.owner_kind = (manifest_owner_kind_t)99;
  rc = manifest_default_synthesise(&p, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_ERR_INVALID_FIELD, "neg_bogus_owner_kind");

  fprintf(stdout, "TEST:PASS:manifest_default_synthesise:negatives\n");
}

static void test_buffer_too_small(void) {
  manifest_default_params_t p;
  /* Use an overlarge backing buffer with sentinels past `cap` so we can
   * assert no overrun occurred. */
  unsigned char backing[256];
  size_t cap;
  size_t out_len = 999u;
  manifest_default_result_t rc;

  fill_default_params(&p);

  for (cap = 1u; cap < 64u; ++cap) {
    /* Initialise full backing buffer to a known sentinel byte so a
     * write past `cap` shows up as a non-sentinel value. */
    memset(backing, 0xa5, sizeof(backing));
    out_len = 999u;
    rc = manifest_default_synthesise(&p, (char *)backing, cap, &out_len);
    CHECK(rc == MANIFEST_DEFAULT_ERR_BUFFER_TOO_SMALL, "neg_buffer_too_small");
    /* No bytes past `cap` should have been touched. */
    size_t i;
    for (i = cap; i < sizeof(backing); ++i) {
      if (backing[i] != 0xa5u) {
        fprintf(stderr,
                "TEST:FAIL:manifest_default_synthesise:buffer_overrun_at_cap_%zu_idx_%zu\n",
                cap, i);
        g_fail = 1;
        break;
      }
    }
  }

  /* out_cap == 0 → INVALID_ARG (not BUFFER_TOO_SMALL — there's no buffer
   * to even attempt a write into). */
  rc = manifest_default_synthesise(&p, (char *)backing, 0u, &out_len);
  CHECK(rc == MANIFEST_DEFAULT_ERR_INVALID_ARG, "neg_zero_cap");

  fprintf(stdout, "TEST:PASS:manifest_default_synthesise:buffer_too_small\n");
}

static void test_local_kind(void) {
  manifest_default_params_t p;
  char out[1024];
  size_t out_len = 0u;
  manifest_default_result_t rc;

  fill_default_params(&p);
  p.owner_kind = MANIFEST_OWNER_KIND_LOCAL;

  rc = manifest_default_synthesise(&p, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_OK, "local_kind_ok");
  CHECK(strstr(out, "\"kind\": \"local\"") != NULL, "local_kind_emits_local");

  /* Schema acceptance of the produced bytes is the .sh peer's job; until
   * #522 lands, the peer SKIP-marks the validator round-trip. The C test
   * just pins that the synthesiser writes the right enumerator string. */
  fprintf(stdout, "TEST:PASS:manifest_default_synthesise:local_kind_emits_local\n");
}

/* Optional driver mode: when invoked with argv[1] = path, write the
 * happy-path synthesised JSON bytes to that file and exit. This lets the
 * .sh peer get the bytes onto disk without re-implementing the
 * synthesiser. argv[2] (optional) selects an owner_kind override: one of
 * "internal", "external", "local". */
static int driver_mode(int argc, char **argv) {
  if (argc < 2) {
    return 0;
  }
  manifest_default_params_t p;
  char out[2048];
  size_t out_len = 0u;
  FILE *fp;
  manifest_default_result_t rc;

  fill_default_params(&p);
  if (argc >= 3) {
    if (strcmp(argv[2], "internal") == 0) {
      p.owner_kind = MANIFEST_OWNER_KIND_INTERNAL;
    } else if (strcmp(argv[2], "external") == 0) {
      p.owner_kind = MANIFEST_OWNER_KIND_EXTERNAL;
    } else if (strcmp(argv[2], "local") == 0) {
      p.owner_kind = MANIFEST_OWNER_KIND_LOCAL;
    } else {
      fprintf(stderr,
              "TEST:FAIL:manifest_default_synthesise:driver_bad_owner_kind:%s\n",
              argv[2]);
      return 2;
    }
  }
  rc = manifest_default_synthesise(&p, out, sizeof(out), &out_len);
  if (rc != MANIFEST_DEFAULT_OK) {
    fprintf(stderr,
            "TEST:FAIL:manifest_default_synthesise:driver_synthesise_rc=%d\n",
            (int)rc);
    return 2;
  }
  fp = fopen(argv[1], "wb");
  if (fp == NULL) {
    fprintf(stderr,
            "TEST:FAIL:manifest_default_synthesise:driver_open_failed:%s\n",
            argv[1]);
    return 2;
  }
  if (fwrite(out, 1, out_len, fp) != out_len) {
    fprintf(stderr,
            "TEST:FAIL:manifest_default_synthesise:driver_write_short\n");
    fclose(fp);
    return 2;
  }
  fclose(fp);
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 2) {
    return driver_mode(argc, argv);
  }
  test_signature_pin();
  test_determinism();
  test_negatives();
  test_buffer_too_small();
  test_local_kind();
  if (g_fail) {
    return 1;
  }
  fprintf(stdout, "TEST:PASS:manifest_default_synthesise\n");
  return 0;
}
