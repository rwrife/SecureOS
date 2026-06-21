/**
 * @file include/manifestgen/manifest_default.h
 * @brief In-OS minimal-manifest synthesiser (M7-TOOLCHAIN-006 sub-slice of
 *        #533 / #409).
 *
 * Purpose:
 *   Plan `plans/2026-05-28-in-os-toolchain-self-hosting.md` §"In-OS packaging"
 *   says the in-OS `cc` driver must synthesise a minimal manifest for the
 *   binary it just compiled on-target:
 *     - `owner.kind = "local"` (the third enumerator landing via #522)
 *     - `abi.version` = the running `OS_ABI_VERSION`
 *     - `capabilities.request` = empty (capability grants are an explicit
 *       follow-up; in-OS builds start un-privileged)
 *
 *   `libmanifestgen` is the freestanding userland-callable factoring of that
 *   logic. It emits a deterministic JSON document conforming to
 *   `manifests/schema/v0.json` without depending on any kernel/crypto/host
 *   header — the `cc` driver (#409) links it after `sofpack_wrap()` to
 *   produce the on-disk `<binary>.manifest.json` companion file.
 *
 *   Same library-extraction-before-driver shape as #521 (sofpack lib) and
 *   PR #519 (`config-secureos.h` for #408). Pre-extracting this leaves #409
 *   with just thin glue (arg-parse + `os_fs_read_file` + `tcc_compile`
 *   + `sofpack_wrap` + this synthesiser + `os_fs_write_file`).
 *
 * Interactions:
 *   - `user/apps/cc/main.c` (#409 driver) will call
 *     `manifest_default_synthesise()` after `sofpack_wrap()` to emit the
 *     companion manifest.
 *   - `tests/manifest_default_synthesise_test.c` exercises the synthesiser
 *     against `manifests/schema/v0.json` via the existing
 *     `tools/validate_manifests.py` wrapper so any schema drift surfaces
 *     here, not at first-run on-target.
 *   - `owner.kind = "local"` is the additive enumerator landing in #522.
 *     Until #522 merges to `main`, the test's `local_kind` arm SKIPs with
 *     the canonical `:awaiting_522` reason marker (same discipline as the
 *     §5.4 `audit_*_recorded` and `toolchain_*` markers in
 *     `validate_m7_markers`).
 *
 * Launched by:
 *   Header-only contract; implementation in `src/manifest_default.c` is
 *   archived into `libmanifestgen.a` by `build/scripts/build_user_lib.sh
 *   manifestgen` (today: host test + future: the `cc` driver app).
 */

#ifndef SECUREOS_MANIFESTGEN_MANIFEST_DEFAULT_H
#define SECUREOS_MANIFESTGEN_MANIFEST_DEFAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- owner.kind enumerator -------------------------------------------- */
/* Mirrors the on-disk `manifests/schema/v0.json` `owner.kind` enum. The
 * third arm (`LOCAL`) lands additively in #522; this header pre-declares
 * it so the in-OS `cc` driver (#409) doesn't need a follow-up source
 * change when #522 merges. The host test's `local_kind` arm SKIPs until
 * the schema accepts "local" (see manifest_default.c). */
typedef enum {
  MANIFEST_OWNER_KIND_INTERNAL = 0,
  MANIFEST_OWNER_KIND_EXTERNAL = 1,
  MANIFEST_OWNER_KIND_LOCAL    = 2,
} manifest_owner_kind_t;

/* ---- Result codes ------------------------------------------------------ */
/* Same naming shape as `sofpack_result_t` so the `cc` driver can compose
 * the two without inventing a third result-code vocabulary. */
typedef enum {
  MANIFEST_DEFAULT_OK                   = 0,
  MANIFEST_DEFAULT_ERR_INVALID_ARG      = 1,
  MANIFEST_DEFAULT_ERR_BUFFER_TOO_SMALL = 2,
  MANIFEST_DEFAULT_ERR_INVALID_FIELD    = 3,
} manifest_default_result_t;

/* ---- Synthesise parameters -------------------------------------------- */
/* The schema-required `app` fields (`id`, `version`, `subject_id`,
 * `binary`) are caller-supplied: the in-OS `cc` driver derives them from
 * the output path it was given, the running build-time identity, and the
 * subject_id assigned by the launcher at spawn. Optional fields are
 * NULL-tolerant and simply omitted from the produced JSON. */
typedef struct {
  uint32_t              abi_version;     /* OS_ABI_VERSION value */
  manifest_owner_kind_t owner_kind;
  const char           *app_id;          /* required; v0 schema pattern */
  const char           *app_version;     /* required; non-empty */
  uint8_t               subject_id;      /* required; 1..7 at v0 */
  const char           *binary_path;     /* required; non-empty */
} manifest_default_params_t;

/* ---- Public API -------------------------------------------------------- */

/**
 * Synthesise a minimal v0 manifest JSON document for the supplied params.
 *
 * On success, writes the produced bytes (NUL-terminated for caller
 * convenience; the terminator is NOT counted in `*out_len`) into
 * `out_buf` and sets `*out_len` to the byte count of the JSON document.
 *
 * The output is deterministic: two calls with identical parameters emit
 * byte-identical bytes. Key order matches the schema's documented order
 * (manifest_version, os_abi_version, app, capabilities, owner). Whitespace
 * is fixed (2-space indent, '\n' newlines).
 *
 * The output is constructed to validate cleanly against
 * `manifests/schema/v0.json` when `owner_kind` is INTERNAL or EXTERNAL.
 * `MANIFEST_OWNER_KIND_LOCAL` will validate once #522 lands; until then
 * the host test's `local_kind` arm SKIPs (the synthesiser itself does
 * NOT change once #522 merges — only the test promotes SKIP to PASS).
 *
 * Returns:
 *   - MANIFEST_DEFAULT_OK                   on success
 *   - MANIFEST_DEFAULT_ERR_INVALID_ARG      params/out_buf/out_len NULL,
 *                                           or any required string is NULL
 *   - MANIFEST_DEFAULT_ERR_INVALID_FIELD    required string empty, or
 *                                           subject_id outside [1, 7], or
 *                                           owner_kind outside the enum
 *   - MANIFEST_DEFAULT_ERR_BUFFER_TOO_SMALL out_cap too small for the
 *                                           produced JSON (including the
 *                                           trailing NUL terminator)
 */
manifest_default_result_t manifest_default_synthesise(
    const manifest_default_params_t *params,
    char                            *out_buf,
    size_t                           out_cap,
    size_t                          *out_len);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_MANIFESTGEN_MANIFEST_DEFAULT_H */
