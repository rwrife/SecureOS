/**
 * @file user/apps/cc/manifest_resolution.c
 * @brief Manifest-resolution precedence helper for in-OS `cc` driver wiring.
 *
 * Purpose:
 *   Implements issue #634 precedence as a single resolver:
 *     --manifest path (cli) > sibling sidecar > libmanifestgen synthesis.
 *
 * Notes:
 *   - This helper keeps loaded manifest bytes verbatim.
 *   - Synthesis path delegates to `manifest_default_synthesise` from
 *     `user/libs/manifestgen` (no byte re-implementation here).
 *   - On synth, writes `<output>.manifest.json` and returns the canonical
 *     `manifest.synth.ok` audit marker token to the caller.
 */

#include "manifest_resolution.h"

#include <stdio.h>
#include <string.h>

static int cc_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int cc_manifest_like_json(const char *buf, size_t len) {
  size_t i = 0u;
  size_t j;

  if (buf == NULL || len == 0u) {
    return 0;
  }

  while (i < len && cc_is_space(buf[i])) {
    ++i;
  }
  if (i == len || buf[i] != '{') {
    return 0;
  }

  j = len;
  while (j > i && cc_is_space(buf[j - 1u])) {
    --j;
  }
  if (j <= i || buf[j - 1u] != '}') {
    return 0;
  }

  if (strstr(buf, "\"manifest_version\"") == NULL) {
    return 0;
  }
  if (strstr(buf, "\"app\"") == NULL) {
    return 0;
  }

  return 1;
}

static int cc_path_copy(char *dst, size_t cap, const char *src) {
  int n;
  if (dst == NULL || src == NULL || cap == 0u) {
    return 0;
  }
  n = snprintf(dst, cap, "%s", src);
  if (n < 0) {
    return 0;
  }
  return (size_t)n < cap;
}

static int cc_sidecar_path(char *out, size_t cap, const char *binary_path) {
  int n;
  if (out == NULL || binary_path == NULL || cap == 0u) {
    return 0;
  }
  n = snprintf(out, cap, "%s%s", binary_path, CC_MANIFEST_SIDECAR_SUFFIX);
  if (n < 0) {
    return 0;
  }
  return (size_t)n < cap;
}

static int cc_file_exists(const char *path) {
  FILE *fp;
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
  fp = fopen(path, "rb");
  if (fp == NULL) {
    return 0;
  }
  fclose(fp);
  return 1;
}

static cc_manifest_resolve_status_t cc_read_file(
    const char *path,
    char *buf,
    size_t cap,
    size_t *out_len) {
  FILE *fp;
  size_t n;
  int ended;

  if (path == NULL || buf == NULL || out_len == NULL || cap < 2u) {
    return CC_MANIFEST_RESOLVE_ERR_INVALID_ARG;
  }

  fp = fopen(path, "rb");
  if (fp == NULL) {
    return CC_MANIFEST_RESOLVE_ERR_READ;
  }

  n = fread(buf, 1, cap - 1u, fp);
  ended = feof(fp);
  if (ferror(fp)) {
    fclose(fp);
    return CC_MANIFEST_RESOLVE_ERR_READ;
  }
  fclose(fp);

  if (!ended) {
    return CC_MANIFEST_RESOLVE_ERR_BUFFER_TOO_SMALL;
  }

  buf[n] = '\0';
  *out_len = n;
  return CC_MANIFEST_RESOLVE_OK;
}

static cc_manifest_resolve_status_t cc_write_file(
    const char *path,
    const char *buf,
    size_t len) {
  FILE *fp;
  if (path == NULL || buf == NULL) {
    return CC_MANIFEST_RESOLVE_ERR_INVALID_ARG;
  }

  fp = fopen(path, "wb");
  if (fp == NULL) {
    return CC_MANIFEST_RESOLVE_ERR_WRITE;
  }

  if (len > 0u && fwrite(buf, 1, len, fp) != len) {
    fclose(fp);
    return CC_MANIFEST_RESOLVE_ERR_WRITE;
  }

  if (fclose(fp) != 0) {
    return CC_MANIFEST_RESOLVE_ERR_WRITE;
  }
  return CC_MANIFEST_RESOLVE_OK;
}

static cc_manifest_resolve_status_t cc_load_manifest(
    const char *path,
    cc_manifest_resolution_t *out) {
  cc_manifest_resolve_status_t st;

  st = cc_read_file(path, out->manifest_bytes,
                    sizeof(out->manifest_bytes),
                    &out->manifest_len);
  if (st != CC_MANIFEST_RESOLVE_OK) {
    return st;
  }
  if (!cc_manifest_like_json(out->manifest_bytes, out->manifest_len)) {
    return CC_MANIFEST_RESOLVE_ERR_INVALID_JSON;
  }
  return CC_MANIFEST_RESOLVE_OK;
}

const char *cc_manifest_source_tag(cc_manifest_source_t source) {
  switch (source) {
    case CC_MANIFEST_SOURCE_CLI:
      return "cli";
    case CC_MANIFEST_SOURCE_SIDECAR:
      return "sidecar";
    case CC_MANIFEST_SOURCE_SYNTH:
      return "synth";
    default:
      return "unknown";
  }
}

cc_manifest_resolve_status_t cc_manifest_resolve(
    const cc_manifest_resolve_params_t *params,
    cc_manifest_resolution_t *out) {
  char sidecar_path[CC_MANIFEST_MAX_PATH];
  cc_manifest_resolve_status_t st;

  if (params == NULL || out == NULL || params->output_binary_path == NULL
      || params->output_binary_path[0] == '\0') {
    return CC_MANIFEST_RESOLVE_ERR_INVALID_ARG;
  }

  memset(out, 0, sizeof(*out));

  if (!cc_sidecar_path(sidecar_path, sizeof(sidecar_path),
                       params->output_binary_path)) {
    return CC_MANIFEST_RESOLVE_ERR_PATH_TOO_LONG;
  }

  /* Branch 1: explicit --manifest path wins unconditionally. */
  if (params->manifest_override_path != NULL
      && params->manifest_override_path[0] != '\0') {
    st = cc_load_manifest(params->manifest_override_path, out);
    if (st != CC_MANIFEST_RESOLVE_OK) {
      return st;
    }
    out->source = CC_MANIFEST_SOURCE_CLI;
    out->provenance_tag = cc_manifest_source_tag(out->source);
    if (!cc_path_copy(out->manifest_path, sizeof(out->manifest_path),
                      params->manifest_override_path)) {
      return CC_MANIFEST_RESOLVE_ERR_PATH_TOO_LONG;
    }
    return CC_MANIFEST_RESOLVE_OK;
  }

  /* Branch 2: existing sibling sidecar. */
  if (cc_file_exists(sidecar_path)) {
    st = cc_load_manifest(sidecar_path, out);
    if (st != CC_MANIFEST_RESOLVE_OK) {
      return st;
    }
    out->source = CC_MANIFEST_SOURCE_SIDECAR;
    out->provenance_tag = cc_manifest_source_tag(out->source);
    if (!cc_path_copy(out->manifest_path, sizeof(out->manifest_path), sidecar_path)) {
      return CC_MANIFEST_RESOLVE_ERR_PATH_TOO_LONG;
    }
    return CC_MANIFEST_RESOLVE_OK;
  }

  /* Branch 3: synthesize via libmanifestgen and persist sidecar. */
  {
    manifest_default_params_t synth_params;
    manifest_default_result_t rc;

    if (params->app_id == NULL || params->app_version == NULL
        || params->app_id[0] == '\0' || params->app_version[0] == '\0') {
      return CC_MANIFEST_RESOLVE_ERR_INVALID_ARG;
    }

    memset(&synth_params, 0, sizeof(synth_params));
    synth_params.abi_version = params->abi_version;
    synth_params.owner_kind = params->owner_kind;
    synth_params.app_id = params->app_id;
    synth_params.app_version = params->app_version;
    synth_params.subject_id = params->subject_id;
    synth_params.binary_path = params->output_binary_path;

    rc = manifest_default_synthesise(&synth_params,
                                     out->manifest_bytes,
                                     sizeof(out->manifest_bytes),
                                     &out->manifest_len);
    if (rc != MANIFEST_DEFAULT_OK) {
      return CC_MANIFEST_RESOLVE_ERR_SYNTH;
    }

    st = cc_write_file(sidecar_path, out->manifest_bytes, out->manifest_len);
    if (st != CC_MANIFEST_RESOLVE_OK) {
      return st;
    }

    out->source = CC_MANIFEST_SOURCE_SYNTH;
    out->provenance_tag = cc_manifest_source_tag(out->source);
    out->audit_marker = "manifest.synth.ok";
    if (!cc_path_copy(out->manifest_path, sizeof(out->manifest_path), sidecar_path)) {
      return CC_MANIFEST_RESOLVE_ERR_PATH_TOO_LONG;
    }
  }

  return CC_MANIFEST_RESOLVE_OK;
}
