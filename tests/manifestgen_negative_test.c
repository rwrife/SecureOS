/**
 * @file tests/manifestgen_negative_test.c
 * @brief Host negative-contract test for libmanifestgen input validation
 *        (issue #592).
 *
 * Purpose:
 *   Pins deterministic failure behavior for malformed libmanifestgen inputs:
 *   owner.kind, runtime.arena_bytes range, caps_required_count bound,
 *   output path validity, and output-buffer-too-small handling.
 *
 * Contract pins:
 *   - Non-zero error code from `manifest_default_result_t` aliases
 *     (`MANIFESTGEN_ERR_*`).
 *   - Stable failure reason token via `manifest_default_audit_fail_reason()`.
 *   - No partial write to output buffer on any non-zero return.
 *   - `out_len` remains unchanged on any non-zero return.
 *
 * Launched by:
 *   build/scripts/test_manifestgen_negative.sh
 *   (dispatch target: manifestgen_negative).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../user/libs/manifestgen/include/manifestgen/manifest_default.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:manifestgen_negative:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

static void fill_default_params(manifest_default_params_t *p) {
  memset(p, 0, sizeof(*p));
  p->abi_version = 0u;
  p->owner_kind = MANIFEST_OWNER_KIND_INTERNAL;
  p->app_id = "helloapp";
  p->app_version = "0.1.0";
  p->subject_id = 2u;
  p->binary_path = "apps/helloapp.bin";
}

static int bytes_unchanged(const unsigned char *before,
                           const unsigned char *after,
                           size_t n) {
  return memcmp(before, after, n) == 0;
}

static void expect_failure_case(
    const char *name,
    const manifest_default_params_t *params,
    size_t out_cap,
    manifest_default_result_t expected_rc,
    const char *expected_reason) {
  unsigned char out[512];
  unsigned char baseline[512];
  size_t out_len = 0xDEADBEEFu;
  size_t sentinel_out_len = out_len;
  manifest_default_result_t rc;
  const char *reason;

  memset(out, 0xA5, sizeof(out));
  memcpy(baseline, out, sizeof(out));

  rc = manifest_default_synthesise(params, (char *)out, out_cap, &out_len);
  CHECK(rc == expected_rc, name);
  CHECK(out_len == sentinel_out_len, "out_len_unchanged_on_error");

  reason = manifest_default_audit_fail_reason(rc, params);
  CHECK(reason != NULL, "reason_not_null");
  CHECK(strcmp(reason, expected_reason) == 0, "reason_matches_expected");

  /* For all non-zero returns, output bytes must be unchanged. */
  CHECK(bytes_unchanged(baseline, out, sizeof(out)), "output_buffer_unchanged_on_error");

  fprintf(stdout, "TEST:PASS:manifestgen_negative:%s\n", name);
}

static void test_owner_kind_invalid(void) {
  manifest_default_params_t p;
  fill_default_params(&p);
  p.owner_kind = (manifest_owner_kind_t)99;
  expect_failure_case("owner_kind_invalid",
                      &p,
                      512u,
                      MANIFESTGEN_ERR_OWNER_KIND_INVALID,
                      "bad_owner_kind");
}

static void test_arena_out_of_range(void) {
  manifest_default_params_t p;

  fill_default_params(&p);
  p.runtime_arena_bytes = MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES_MAX + 1u;
  expect_failure_case("arena_above_max",
                      &p,
                      512u,
                      MANIFESTGEN_ERR_ARENA_OUT_OF_RANGE,
                      "bad_arena_bytes");

  fill_default_params(&p);
  p.runtime_arena_bytes = (uint32_t)SIZE_MAX;
  expect_failure_case("arena_size_max",
                      &p,
                      512u,
                      MANIFESTGEN_ERR_ARENA_OUT_OF_RANGE,
                      "bad_arena_bytes");
}

static void test_caps_required_too_many(void) {
  manifest_default_params_t p;
  fill_default_params(&p);
  p.caps_required_count = MANIFEST_DEFAULT_CAPS_REQUIRED_MAX + 1u;
  expect_failure_case("caps_required_too_many",
                      &p,
                      512u,
                      MANIFESTGEN_ERR_CAPS_TOO_MANY,
                      "bad_caps_required_count");
}

static void test_path_invalid(void) {
  manifest_default_params_t p;
  fill_default_params(&p);
  p.binary_path = "";
  expect_failure_case("path_empty",
                      &p,
                      512u,
                      MANIFESTGEN_ERR_PATH_INVALID,
                      "bad_output_path");
}

static void test_output_too_small(void) {
  manifest_default_params_t p;
  fill_default_params(&p);
  expect_failure_case("output_too_small",
                      &p,
                      2u,
                      MANIFESTGEN_ERR_OUTPUT_TOO_SMALL,
                      "output_too_small");
}

int main(void) {
  test_owner_kind_invalid();
  test_arena_out_of_range();
  test_caps_required_too_many();
  test_path_invalid();
  test_output_too_small();

  if (g_fail) {
    return 1;
  }

  fprintf(stdout, "TEST:PASS:manifestgen_negative\n");
  return 0;
}
