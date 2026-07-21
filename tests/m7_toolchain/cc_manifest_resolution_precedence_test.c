/**
 * @file tests/m7_toolchain/cc_manifest_resolution_precedence_test.c
 * @brief Host table-driven tests for `cc_manifest_resolve` precedence (issue #634).
 *
 * Covers:
 *   1) `--manifest <path>` wins over sibling sidecar.
 *   2) Sidecar is used when no CLI override is supplied.
 *   3) Synth branch calls libmanifestgen and writes `<output>.manifest.json`.
 *   4) Invalid CLI override is a hard fail (no silent fallback to sidecar).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../user/apps/cc/manifest_resolution.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:cc_manifest_resolution_precedence:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

static int write_text(const char *path, const char *text) {
  FILE *fp = fopen(path, "wb");
  size_t n;
  if (fp == NULL) {
    return 0;
  }
  n = strlen(text);
  if (n > 0u && fwrite(text, 1, n, fp) != n) {
    fclose(fp);
    return 0;
  }
  fclose(fp);
  return 1;
}

static int read_text(const char *path, char *out, size_t cap) {
  FILE *fp;
  size_t n;
  int ended;

  if (out == NULL || cap < 2u) {
    return 0;
  }

  fp = fopen(path, "rb");
  if (fp == NULL) {
    return 0;
  }

  n = fread(out, 1, cap - 1u, fp);
  ended = feof(fp);
  if (ferror(fp)) {
    fclose(fp);
    return 0;
  }
  fclose(fp);

  if (!ended) {
    return 0;
  }

  out[n] = '\0';
  return 1;
}

static void rm_if_exists(const char *path) {
  if (path != NULL) {
    (void)unlink(path);
  }
}

static int path_join(char *out, size_t cap, const char *a, const char *b) {
  int n;
  if (out == NULL || a == NULL || b == NULL || cap == 0u) {
    return 0;
  }
  n = snprintf(out, cap, "%s/%s", a, b);
  return n >= 0 && (size_t)n < cap;
}

typedef struct {
  const char *name;
  const char *override_body;   /* NULL => omit --manifest */
  const char *sidecar_body;    /* NULL => no pre-existing sidecar */
  cc_manifest_source_t expected_source;
} precedence_case_t;

static void run_precedence_table(const char *tmp_dir) {
  static const char *kCliManifest =
      "{\n"
      "  \"manifest_version\": 0,\n"
      "  \"app\": {\"id\": \"cli\"}\n"
      "}\n";
  static const char *kSidecarManifest =
      "{\n"
      "  \"manifest_version\": 0,\n"
      "  \"app\": {\"id\": \"sidecar\"}\n"
      "}\n";

  const precedence_case_t cases[] = {
      {"cli_wins", kCliManifest, kSidecarManifest, CC_MANIFEST_SOURCE_CLI},
      {"sidecar_used", NULL, kSidecarManifest, CC_MANIFEST_SOURCE_SIDECAR},
      {"synth_fallback", NULL, NULL, CC_MANIFEST_SOURCE_SYNTH},
  };

  char output_path[CC_MANIFEST_MAX_PATH];
  char sidecar_path[CC_MANIFEST_MAX_PATH];
  char override_path[CC_MANIFEST_MAX_PATH];
  char observed_sidecar[CC_MANIFEST_MAX_BYTES];
  size_t i;

  CHECK(path_join(output_path, sizeof(output_path), tmp_dir, "hello.bin"),
        "path_output");
  CHECK(path_join(override_path, sizeof(override_path), tmp_dir, "override.json"),
        "path_override");
  CHECK(path_join(sidecar_path, sizeof(sidecar_path), tmp_dir,
                  "hello.bin.manifest.json"),
        "path_sidecar");

  for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    cc_manifest_resolve_params_t params;
    cc_manifest_resolution_t out;
    cc_manifest_resolve_status_t st;

    rm_if_exists(override_path);
    rm_if_exists(sidecar_path);

    if (cases[i].override_body != NULL) {
      CHECK(write_text(override_path, cases[i].override_body), "write_override");
    }
    if (cases[i].sidecar_body != NULL) {
      CHECK(write_text(sidecar_path, cases[i].sidecar_body), "write_sidecar");
    }

    memset(&params, 0, sizeof(params));
    params.output_binary_path = output_path;
    params.manifest_override_path = (cases[i].override_body != NULL) ? override_path : NULL;
    params.abi_version = 0u;
    params.owner_kind = MANIFEST_OWNER_KIND_LOCAL;
    params.app_id = "helloapp";
    params.app_version = "0.1.0";
    params.subject_id = 2u;

    memset(&out, 0, sizeof(out));
    st = cc_manifest_resolve(&params, &out);
    CHECK(st == CC_MANIFEST_RESOLVE_OK, "resolve_ok");
    if (st != CC_MANIFEST_RESOLVE_OK) {
      continue;
    }

    CHECK(out.source == cases[i].expected_source, "source_matches");
    CHECK(strcmp(out.provenance_tag, cc_manifest_source_tag(out.source)) == 0,
          "provenance_tag_matches_source");

    if (cases[i].expected_source == CC_MANIFEST_SOURCE_CLI) {
      CHECK(strcmp(out.manifest_bytes, kCliManifest) == 0, "cli_bytes_verbatim");
      CHECK(read_text(sidecar_path, observed_sidecar, sizeof(observed_sidecar)),
            "cli_sidecar_still_readable");
      CHECK(strcmp(observed_sidecar, kSidecarManifest) == 0,
            "cli_does_not_mutate_sidecar");
      CHECK(out.audit_marker == NULL, "cli_no_synth_audit_marker");
    } else if (cases[i].expected_source == CC_MANIFEST_SOURCE_SIDECAR) {
      CHECK(strcmp(out.manifest_bytes, kSidecarManifest) == 0,
            "sidecar_bytes_verbatim");
      CHECK(out.audit_marker == NULL, "sidecar_no_synth_audit_marker");
    } else {
      CHECK(strstr(out.manifest_bytes, "\"kind\": \"local\"") != NULL,
            "synth_contains_local_owner_kind");
      CHECK(out.audit_marker != NULL, "synth_audit_marker_present");
      if (out.audit_marker != NULL) {
        CHECK(strcmp(out.audit_marker, "manifest.synth.ok") == 0,
              "synth_audit_marker_value");
      }
      CHECK(read_text(sidecar_path, observed_sidecar, sizeof(observed_sidecar)),
            "synth_sidecar_written");
      CHECK(strcmp(observed_sidecar, out.manifest_bytes) == 0,
            "synth_sidecar_matches_returned_bytes");
    }

    fprintf(stdout, "TEST:PASS:cc_manifest_resolution_precedence:%s\n", cases[i].name);
  }
}

static void test_invalid_cli_manifest_hard_fails(const char *tmp_dir) {
  static const char *kSidecarManifest =
      "{\n"
      "  \"manifest_version\": 0,\n"
      "  \"app\": {\"id\": \"sidecar\"}\n"
      "}\n";

  char output_path[CC_MANIFEST_MAX_PATH];
  char sidecar_path[CC_MANIFEST_MAX_PATH];
  char override_path[CC_MANIFEST_MAX_PATH];
  char observed_sidecar[CC_MANIFEST_MAX_BYTES];
  cc_manifest_resolve_params_t params;
  cc_manifest_resolution_t out;
  cc_manifest_resolve_status_t st;

  CHECK(path_join(output_path, sizeof(output_path), tmp_dir, "invalid.bin"),
        "invalid_path_output");
  CHECK(path_join(override_path, sizeof(override_path), tmp_dir, "invalid_override.json"),
        "invalid_path_override");
  CHECK(path_join(sidecar_path, sizeof(sidecar_path), tmp_dir,
                  "invalid.bin.manifest.json"),
        "invalid_path_sidecar");

  rm_if_exists(override_path);
  rm_if_exists(sidecar_path);

  CHECK(write_text(override_path, "this is not json\n"), "invalid_write_override");
  CHECK(write_text(sidecar_path, kSidecarManifest), "invalid_write_sidecar");

  memset(&params, 0, sizeof(params));
  params.output_binary_path = output_path;
  params.manifest_override_path = override_path;
  params.abi_version = 0u;
  params.owner_kind = MANIFEST_OWNER_KIND_LOCAL;
  params.app_id = "helloapp";
  params.app_version = "0.1.0";
  params.subject_id = 2u;

  memset(&out, 0, sizeof(out));
  st = cc_manifest_resolve(&params, &out);
  CHECK(st == CC_MANIFEST_RESOLVE_ERR_INVALID_JSON,
        "invalid_cli_manifest_is_hard_error");

  CHECK(read_text(sidecar_path, observed_sidecar, sizeof(observed_sidecar)),
        "invalid_cli_sidecar_still_readable");
  CHECK(strcmp(observed_sidecar, kSidecarManifest) == 0,
        "invalid_cli_does_not_fallback_or_mutate_sidecar");

  fprintf(stdout,
          "TEST:PASS:cc_manifest_resolution_precedence:invalid_cli_hard_fail\n");
}

static int make_temp_dir(char *out, size_t cap) {
  unsigned int i;
  if (out == NULL || cap == 0u) {
    return 0;
  }
  for (i = 0u; i < 256u; ++i) {
    int n = snprintf(out, cap,
                     "/tmp/cc_manifest_precedence_%ld_%u",
                     (long)getpid(), i);
    if (n < 0 || (size_t)n >= cap) {
      return 0;
    }
    if (mkdir(out, 0700) == 0) {
      return 1;
    }
  }
  return 0;
}

int main(void) {
  char tmp_dir[CC_MANIFEST_MAX_PATH];

  if (!make_temp_dir(tmp_dir, sizeof(tmp_dir))) {
    fprintf(stderr,
            "TEST:FAIL:cc_manifest_resolution_precedence:mktempdir_failed\n");
    return 1;
  }

  run_precedence_table(tmp_dir);
  test_invalid_cli_manifest_hard_fails(tmp_dir);

  if (g_fail) {
    return 1;
  }
  fprintf(stdout, "TEST:PASS:cc_manifest_resolution_precedence\n");
  return 0;
}
