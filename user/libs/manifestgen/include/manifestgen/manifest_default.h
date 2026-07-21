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
 *     - `runtime.arena_bytes = 65536` (pinned default policy, #595)
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

/* Canonical sidecar suffix for `<binary>.manifest.json` companion manifests.
 * Mirrors docs/abi/manifest.md §5.8 "Sidecar filename convention". */
#define MANIFEST_SIDECAR_SUFFIX ".manifest.json"

/* Canonical default arena ceiling emitted by libmanifestgen when synthesis is
 * selected (i.e. no explicit `cc --manifest <path>` override and no valid
 * adjacent sidecar). Mirrors docs/abi/manifest.md §5.8 "Default
 * `runtime.arena_bytes` policy". */
#define MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES 65536u

/* Hard upper bound mirrored from manifests/schema/v0.json runtime.arena_bytes
 * maximum (PROC_ARENA_BYTES_MAX). */
#define MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES_MAX 16777216u

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

/* ---- Audit-marker helpers --------------------------------------------- */
/* Issue #594 pins the marker grammar for manifest synthesis:
 *   manifest.synth.ok:<sid>:<sof_sha_prefix>:<owner_kind>:<arena_bytes>
 *   manifest.synth.fail:<sid>:<reason_enum>
 *
 * These helpers are format-only (no runtime emission side effects). They let
 * host tests pin marker shape against docs/abi/audit-markers.md while keeping
 * libmanifestgen freestanding and deterministic. */

/* Largest marker payload this helper emits including trailing NUL.
 * Kept intentionally small and deterministic for stack callers. */
#define MANIFEST_SYNTH_AUDIT_MARKER_MAX 160u

/* Canonical `sof_sha_prefix` width used in marker examples and tests. */
#define MANIFEST_SYNTH_AUDIT_SHA_PREFIX_HEX 12u

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
  uint32_t              runtime_arena_bytes;
  /* Optional explicit runtime arena value. 0 means "use
   * MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES". Non-zero values must satisfy
   * [MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES,
   *  MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES_MAX]. */
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
 * (manifest_version, os_abi_version, app, capabilities, runtime, owner).
 * Whitespace is fixed (2-space indent, '\n' newlines).
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
 *                                           owner_kind outside the enum, or
 *                                           runtime_arena_bytes outside
 *                                           [default, max] when non-zero
 *   - MANIFEST_DEFAULT_ERR_BUFFER_TOO_SMALL out_cap too small for the
 *                                           produced JSON (including the
 *                                           trailing NUL terminator)
 */
manifest_default_result_t manifest_default_synthesise(
    const manifest_default_params_t *params,
    char                            *out_buf,
    size_t                           out_cap,
    size_t                          *out_len);

/* Returns the canonical owner.kind spelling for marker formatting, or NULL
 * when `owner_kind` is outside the declared enum.
 */
const char *manifest_default_owner_kind_tag(manifest_owner_kind_t owner_kind);

/* Maps a synth failure result to a stable reason token for
 * `manifest.synth.fail:<sid>:<reason_enum>` markers.
 */
const char *manifest_default_audit_fail_reason(
    manifest_default_result_t        rc,
    const manifest_default_params_t *params);

/* Format helper for `manifest.synth.ok` marker lines. */
manifest_default_result_t manifest_default_format_audit_marker_ok(
    uint32_t             sid,
    const char          *sof_sha_prefix,
    manifest_owner_kind_t owner_kind,
    uint32_t             arena_bytes,
    char                *out_buf,
    size_t               out_cap,
    size_t              *out_len);

/* Format helper for `manifest.synth.fail` marker lines. */
manifest_default_result_t manifest_default_format_audit_marker_fail(
    uint32_t                       sid,
    manifest_default_result_t      rc,
    const manifest_default_params_t *params,
    char                           *out_buf,
    size_t                          out_cap,
    size_t                         *out_len);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_MANIFESTGEN_MANIFEST_DEFAULT_H */
