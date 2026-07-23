/**
 * @file tests/manifestgen_audit_marker_format_test.c
 * @brief Host format-contract pin for libmanifestgen synth audit markers
 *        (issue #594).
 *
 * Purpose:
 *   Pins exact marker line shapes for manifest synthesis outcomes without
 *   changing runtime emitter behavior:
 *     - manifest.synth.ok:<sid>:<sof_sha_prefix>:<owner_kind>:<arena_bytes>
 *     - manifest.synth.fail:<sid>:<reason_enum>
 *
 * Coverage:
 *   - Success formatting (`manifest.synth.ok`) from a valid synth path.
 *   - Failure formatting (`manifest.synth.fail`) for representative
 *     error branches (owner-kind invalid, arena invalid, required-field
 *     invalid, invalid args).
 *   - Basic API guardrails (reject fail-marker formatting for rc=OK).
 *
 * Launched by:
 *   build/scripts/test_manifestgen_audit_marker_format.sh
 *   (dispatch target: manifestgen_audit_marker_format).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../user/libs/manifestgen/include/manifestgen/manifest_default.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:manifestgen_audit_marker_format:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

static void fill_default_params(manifest_default_params_t *p) {
  memset(p, 0, sizeof(*p));
  p->abi_version = 0u;
  p->owner_kind = MANIFEST_OWNER_KIND_LOCAL;
  p->app_id = "helloapp";
  p->app_version = "0.1.0";
  p->subject_id = 2u;
  p->binary_path = "apps/helloapp.bin";
}

static void expect_fail_marker(
    const char *subname,
    manifest_default_result_t rc,
    const manifest_default_params_t *params,
    const char *expected_marker) {
  char marker[MANIFEST_SYNTH_AUDIT_MARKER_MAX];
  size_t marker_len = 0u;
  manifest_default_result_t st;

  st = manifest_default_format_audit_marker_fail(
      42u, rc, params, marker, sizeof(marker), &marker_len);
  CHECK(st == MANIFEST_DEFAULT_OK, "fail_marker_format_ok");
  CHECK(strcmp(marker, expected_marker) == 0, "fail_marker_string_match");
  CHECK(strlen(marker) == marker_len, "fail_marker_len_match");

  fprintf(stdout, "TEST:PASS:manifestgen_audit_marker_format:%s\n", subname);
}

static void test_ok_marker(void) {
  manifest_default_params_t p;
  char out[2048];
  size_t out_len = 0u;
  manifest_default_result_t rc;

  char marker[MANIFEST_SYNTH_AUDIT_MARKER_MAX];
  size_t marker_len = 0u;
  manifest_default_result_t st;

  fill_default_params(&p);
  rc = manifest_default_synthesise(&p, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_OK, "synth_ok_for_ok_marker");
  CHECK(out_len > 0u, "synth_nonzero_len");

  st = manifest_default_format_audit_marker_ok(
      42u,
      "0123456789ab",
      MANIFEST_OWNER_KIND_LOCAL,
      MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES,
      marker,
      sizeof(marker),
      &marker_len);
  CHECK(st == MANIFEST_DEFAULT_OK, "ok_marker_format_ok");
  CHECK(strcmp(marker,
               "manifest.synth.ok:42:0123456789ab:local:65536") == 0,
        "ok_marker_string_match");
  CHECK(strlen(marker) == marker_len, "ok_marker_len_match");

  fprintf(stdout, "TEST:PASS:manifestgen_audit_marker_format:ok_marker\n");
}

static void test_fail_markers(void) {
  manifest_default_params_t p;
  char out[2048];
  size_t out_len = 0u;
  manifest_default_result_t rc;

  /* bad_owner_kind */
  fill_default_params(&p);
  p.owner_kind = (manifest_owner_kind_t)99;
  rc = manifest_default_synthesise(&p, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_ERR_OWNER_KIND_INVALID, "rc_bad_owner_kind");
  expect_fail_marker(
      "fail_bad_owner_kind",
      rc,
      &p,
      "manifest.synth.fail:42:bad_owner_kind");

  /* bad_arena_bytes */
  fill_default_params(&p);
  p.runtime_arena_bytes = MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES_MAX + 1u;
  rc = manifest_default_synthesise(&p, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_ERR_ARENA_OUT_OF_RANGE, "rc_bad_arena");
  expect_fail_marker(
      "fail_bad_arena_bytes",
      rc,
      &p,
      "manifest.synth.fail:42:bad_arena_bytes");

  /* bad_required_fields */
  fill_default_params(&p);
  p.app_id = "";
  rc = manifest_default_synthesise(&p, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_ERR_INVALID_FIELD, "rc_bad_required_fields");
  expect_fail_marker(
      "fail_bad_required_fields",
      rc,
      &p,
      "manifest.synth.fail:42:bad_required_fields");

  /* bad_args */
  fill_default_params(&p);
  rc = manifest_default_synthesise(NULL, out, sizeof(out), &out_len);
  CHECK(rc == MANIFEST_DEFAULT_ERR_INVALID_ARG, "rc_bad_args");
  expect_fail_marker(
      "fail_bad_args",
      rc,
      NULL,
      "manifest.synth.fail:42:bad_args");
}

static void test_guardrails(void) {
  char marker[MANIFEST_SYNTH_AUDIT_MARKER_MAX];
  size_t marker_len = 0u;
  manifest_default_result_t st;

  st = manifest_default_format_audit_marker_fail(
      42u,
      MANIFEST_DEFAULT_OK,
      NULL,
      marker,
      sizeof(marker),
      &marker_len);
  CHECK(st == MANIFEST_DEFAULT_ERR_INVALID_FIELD,
        "fail_marker_rejects_ok_rc");

  fprintf(stdout, "TEST:PASS:manifestgen_audit_marker_format:guardrails\n");
}

int main(void) {
  test_ok_marker();
  test_fail_markers();
  test_guardrails();

  if (g_fail) {
    return 1;
  }
  fprintf(stdout, "TEST:PASS:manifestgen_audit_marker_format\n");
  return 0;
}
