# SecureOS Manifest ABI

This document describes the launcher / app manifest surface that the SDK
consumes when packaging a signed app or library bundle. It is a **forward
contract**: parts of the schema below are still being implemented across
M2 (#82, console/launcher/HelloApp) and M3 (#83, filesystem service), and
field-by-field stability is called out explicitly.

The on-disk container for any signed artifact (OS command, user library, app
bundle) is the SOF (Signed Object Format) wrapper defined in
`kernel/format/sof.h`. The manifest sits **inside** that wrapper as a typed
metadata blob.

## SOF container — stable today

Defined in `kernel/format/sof.h`. The `sof_header_t`, the metadata TLV
entries (`sof_meta_entry_t`, max `SOF_META_MAX_ENTRIES = 12`), and the
following result codes are part of the ABI:

| Code                          | Value | Meaning                                  |
| ----------------------------- | ----- | ---------------------------------------- |
| `SOF_OK`                      | 0     | Parse / validation succeeded.             |
| `SOF_ERR_INVALID_MAGIC`       | 1     | Wrong magic; not a SOF artifact.          |
| `SOF_ERR_INVALID_VERSION`     | 2     | Container version unsupported.            |
| `SOF_ERR_INVALID_TYPE`        | 3     | `file_type` not recognized.               |
| `SOF_ERR_INVALID_SIZE`        | 4     | Size fields inconsistent with the buffer. |
| `SOF_ERR_INVALID_META`        | 5     | Metadata TLV malformed.                   |
| `SOF_ERR_BUFFER_TOO_SMALL`    | 6     | Output buffer too small.                  |
| `SOF_ERR_NO_PAYLOAD`          | 7     | Payload absent.                           |
| `SOF_ERR_SIGNATURE_REQUIRED`  | 8     | Unsigned bundle rejected (no `CAP_CODESIGN_BYPASS`). |
| `SOF_ERR_SIGNATURE_INVALID`   | 9     | Signature present but verification failed. |

Numeric values are append-only.

## Build-time metadata fields (`sof_build_params_t`)

These are the metadata fields that `tools/sof_wrap` writes into the SOF
container at build time:

| Field         | Required | Purpose                                                 |
| ------------- | -------- | ------------------------------------------------------- |
| `file_type`   | yes      | One of the `sof_file_type_t` enumerants (OS command, user library, app bundle). |
| `name`        | yes      | Short identifier; printable ASCII, no `/`.              |
| `description` | yes      | Human-readable summary.                                 |
| `author`      | yes      | Author / publisher attribution.                         |
| `version`     | yes      | SemVer-shaped version string.                           |
| `date`        | yes      | ISO-8601 build date.                                    |
| `icon`        | no       | Optional UI hint; may be NULL.                          |
| `syscall_id`  | no       | OS-command dispatch routing key for `file_type = OS_COMMAND`. |
| `elf_payload` / `elf_payload_size` | yes | The actual code/data. |

These fields are intentionally simple — they are TLV entries in the
SOF metadata block and are read on the kernel side by `sof_parse`.

## App / launcher manifest — provisional schema

The **app bundle** layer wraps zero or more SOF artifacts together with a
launcher manifest. `sof_app_bundle_header_t` reserves the on-wire shape:

```c
typedef struct {
  uint32_t entry_count;
  uint32_t manifest_offset;
  uint32_t manifest_size;
  uint32_t compression_algo;  /* 0 = NONE; 1/2 reserved for LZ4 / ZSTD */
  uint32_t reserved[4];
} sof_app_bundle_header_t;
```

The on-disk encoding of the bundle header is **stable**. The contents of the
manifest body it points to are still being defined under M2 (#82). The
proposed minimal schema, which the SDK should target, is JSON with the
following shape:

```json
{
  "schema_version": 0,
  "name": "HelloApp",
  "version": "0.1.0",
  "entry": "hello.sof",
  "capabilities": {
    "required": ["CAP_CONSOLE_WRITE"],
    "optional": []
  },
  "signing": {
    "publisher": "secureos.dev",
    "key_id": "ed25519:bootstrap-root",
    "policy": "must-verify"
  }
}
```

Field rules (provisional but stable from M2 onwards unless otherwise noted):

- `schema_version` — append-only integer; bumped only on a breaking change.
  At ABI version 0 the launcher accepts `schema_version = 0`.
- `name` / `version` — must match the corresponding SOF metadata of the
  entry binary; mismatch is a manifest-validation failure.
- `entry` — relative path inside the bundle to the SOF artifact the
  launcher should load.
- `capabilities.required` — list of capability **names** (matching the
  table in [`capabilities.md`](./capabilities.md)). Names are used here,
  not numeric IDs, so manifests stay stable across capability ID
  renumbering attempts (which are forbidden anyway, but the redundancy is
  cheap).
- `capabilities.optional` — capabilities the app prefers but can run
  without; the launcher **may** grant a subset.
- `signing.policy` — one of:
  - `"must-verify"` — default; loader rejects on missing or invalid signature
    (returns `SOF_ERR_SIGNATURE_REQUIRED` / `SOF_ERR_SIGNATURE_INVALID`).
  - `"bypass"` — only honored if the loading subject holds
    `CAP_CODESIGN_BYPASS`; bootstrap-only.

### Worked example (HelloApp deny-path, M2)

For the `helloapp_denied_console_write` test (issue #92), the launcher
manifest deliberately omits `CAP_CONSOLE_WRITE`:

```json
{
  "schema_version": 0,
  "name": "HelloApp",
  "version": "0.1.0",
  "entry": "hello.sof",
  "capabilities": { "required": [], "optional": ["CAP_CONSOLE_WRITE"] },
  "signing": { "publisher": "secureos.dev", "key_id": "ed25519:bootstrap-root", "policy": "must-verify" }
}
```

Expected behavior:

1. Launcher loads `hello.sof`, verifies signature.
2. Launcher allocates a subject id, grants no capabilities (the
   `required` list is empty; `optional` does not auto-grant).
3. App calls `os_console_write(...)` → `OS_STATUS_DENIED`.
4. Audit ring contains a `CAP_AUDIT_OP_CHECK` event with
   `capability_id = CAP_CONSOLE_WRITE`, `result = CAP_ERR_MISSING`.

## Compatibility policy

- Adding a manifest field is backward-compatible iff the field has a
  documented default and the loader treats absence as that default.
- Removing or renaming a field is breaking and requires bumping
  `schema_version` and providing a one-major-version compat shim per
  [`versioning.md`](./versioning.md).
- The SOF binary layout (`sof_header_t`, TLV grammar,
  `sof_app_bundle_header_t`) does not move within `OS_ABI_VERSION` 0; any
  change there is automatically a major bump.

## Outstanding items

- Final wire format for `manifest_body` (JSON vs. binary TLV) is being
  decided in M2 (#82). This document will be refreshed once a PR lands.
- `signing.key_id` semantics depend on the corrected ed25519 stack landing
  (#133 / PR #134) and the re-sign discipline tracked in #138.

Last verified against commit: `9f4f7cc` (2026-05-15).
