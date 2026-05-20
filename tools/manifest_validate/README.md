# tools/manifest_validate/

Stdlib-only JSON-Schema validator for SecureOS manifests
(`*.manifest.json`). Used by `build/scripts/validate_manifests.sh` and
the `manifest-schema-validate` step in the PR validation workflow
(issue #195).

## Why a custom validator?

- The toolchain image (`build/docker/Dockerfile.toolchain`) intentionally
  has no Python JSON-Schema dependency. Pulling in `jsonschema` or
  `check-jsonschema` would mean either re-pinning the toolchain digest
  (heavy ABI churn) or running the schema check outside the container
  (drift risk).
- The schema (`manifests/schema/v0.json`) uses a small, fixed subset of
  JSON-Schema 2020-12 keywords (`type`, `const`, `enum`, `required`,
  `properties`, `additionalProperties: false`, `items`, `pattern`,
  `minLength`, `maxLength`). The validator implements exactly that subset
  and **rejects any schema that uses an unsupported keyword**, so adding
  a new schema feature is a deliberate code change instead of a silent
  no-op.

## Usage

```bash
python3 tools/manifest_validate/validate.py \
    --schema manifests/schema/v0.json \
    manifests/examples/helloapp.manifest.json
```

Or, to walk the whole tree (the CI path):

```bash
build/scripts/validate_manifests.sh
```

External `check-jsonschema` / `ajv` remain valid alternatives for local
development and are documented in `manifests/README.md`.

## Exit codes

| Code | Meaning |
|------|---------|
| 0    | All inputs validated |
| 1    | At least one manifest failed validation (stdout prints `FAIL <path> <pointer> <message>`) |
| 2    | Usage error, missing schema, or invalid JSON in the schema |

## Status

This tool is intentionally tiny (~200 LOC) and lives inside the repo so
that schema/example drift is caught with zero external dependencies. If
the manifest schema grows beyond the supported keyword subset, prefer
extending the validator rather than swapping it for a heavier dependency
unless `Dockerfile.toolchain` is being rebuilt anyway.
