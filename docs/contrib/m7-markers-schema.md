# M7 markers schema pin

Issue: [#611](https://github.com/rwrife/SecureOS/issues/611)

This document pins the additive schema contract for entries in
`tests/m7_toolchain/markers.json`.

Why additive: existing gates (`validate_m7_markers`, `validate_m7_marker_harnesses`)
still consume legacy fields (`name`, `reason`, `description`). This schema gate
adds explicit metadata discipline without breaking that compatibility.

## Entry shape

Each `markers[]` row must include:

```json
{
  "id": "toolchain_cc_version_and_help_text_pinned",
  "name": "toolchain_cc_version_and_help_text_pinned",
  "harnessPath": "tests/m7_toolchain/toolchain_cc_version_and_help_text_pinned.sh",
  "gatingIssue": 409,
  "gatingIssues": [409],
  "reason": "awaiting_409",
  "skipReason": "pre-409",
  "addedIn": "issue-637",
  "description": "Pre-#409 SKIP-pinned golden contract for cc --version/help."
}
```

## Field rules

- `id`
  - required string, pattern `toolchain_[a-z0-9_]+`
  - must be unique
  - must match `name` for now (compatibility with existing validators)
- `harnessPath`
  - required relative path under `tests/m7_toolchain/`
  - suffix must be `.sh`, `.c`, or `.py`
  - file must exist on disk
- `gatingIssue`
  - required positive integer
- `gatingIssues`
  - optional non-empty array of positive integers
  - if present, must include `gatingIssue`
- `skipReason`
  - required enum:
    - `pre-408`
    - `pre-409`
    - `pre-410`
    - `pre-585`
    - `deferred`
- `addedIn`
  - required provenance token, format: `issue-<id>`, `pr-<id>`, or `commit-<sha>`
- `reason` / `description` / `name`
  - required legacy compatibility fields consumed by existing gates

Unknown row keys are rejected by the schema gate.

## Validator and wiring

- Validator tool: `tools/validate_m7_markers_schema.py`
- Wrapper: `build/scripts/validate_m7_markers_schema.sh`
- `test.sh` target: `validate_m7_markers_schema`
- `validate_bundle.sh` `TEST_TARGETS` includes `validate_m7_markers_schema`

Run locally:

```bash
bash build/scripts/test.sh validate_m7_markers_schema
```
