/**
 * @file user/apps/cc/manifest_resolution.h
 * @brief Manifest-resolution precedence helper for the in-OS `cc` driver.
 *
 * Contract (issue #634, refs #607):
 *   1) explicit `--manifest <path>`
 *   2) `<output>.manifest.json` sidecar
 *   3) synthesise via libmanifestgen + persist sidecar
 *
 * This header exposes a single precedence resolver that returns the resolved
 * manifest bytes and provenance tag (`cli|sidecar|synth`).
 */

#ifndef SECUREOS_USER_APPS_CC_MANIFEST_RESOLUTION_H
#define SECUREOS_USER_APPS_CC_MANIFEST_RESOLUTION_H

#include <stddef.h>
#include <stdint.h>

#include "../../libs/manifestgen/include/manifestgen/manifest_default.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CC_MANIFEST_SIDECAR_SUFFIX ".manifest.json"
#define CC_MANIFEST_MAX_BYTES      8192u
#define CC_MANIFEST_MAX_PATH       512u

typedef enum {
  CC_MANIFEST_SOURCE_CLI = 0,
  CC_MANIFEST_SOURCE_SIDECAR = 1,
  CC_MANIFEST_SOURCE_SYNTH = 2,
} cc_manifest_source_t;

typedef enum {
  CC_MANIFEST_RESOLVE_OK = 0,
  CC_MANIFEST_RESOLVE_ERR_INVALID_ARG = 1,
  CC_MANIFEST_RESOLVE_ERR_PATH_TOO_LONG = 2,
  CC_MANIFEST_RESOLVE_ERR_READ = 3,
  CC_MANIFEST_RESOLVE_ERR_WRITE = 4,
  CC_MANIFEST_RESOLVE_ERR_INVALID_JSON = 5,
  CC_MANIFEST_RESOLVE_ERR_SYNTH = 6,
  CC_MANIFEST_RESOLVE_ERR_BUFFER_TOO_SMALL = 7,
} cc_manifest_resolve_status_t;

typedef struct {
  const char *output_binary_path;      /* required */
  const char *manifest_override_path;  /* optional (--manifest); NULL/empty => unset */
  uint32_t abi_version;
  manifest_owner_kind_t owner_kind;
  const char *app_id;                  /* required for synth path */
  const char *app_version;             /* required for synth path */
  uint8_t subject_id;                  /* required for synth path */
} cc_manifest_resolve_params_t;

typedef struct {
  cc_manifest_source_t source;
  const char *provenance_tag;          /* literal: cli|sidecar|synth */
  const char *audit_marker;            /* synth branch emits manifest.synth.ok */
  size_t manifest_len;
  char manifest_path[CC_MANIFEST_MAX_PATH];
  char manifest_bytes[CC_MANIFEST_MAX_BYTES];
} cc_manifest_resolution_t;

const char *cc_manifest_source_tag(cc_manifest_source_t source);

cc_manifest_resolve_status_t cc_manifest_resolve(
    const cc_manifest_resolve_params_t *params,
    cc_manifest_resolution_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_APPS_CC_MANIFEST_RESOLUTION_H */
