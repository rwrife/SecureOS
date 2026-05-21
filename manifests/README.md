# SecureOS manifests

This directory holds machine-readable manifest schemas and worked
examples consumed by SecureOS tooling and the launcher.

## Layout

- `schema/v0.json` — App manifest schema, `OS_ABI_VERSION=0` family.
  See [`docs/abi/manifest.md`](../docs/abi/manifest.md) for the prose
  spec and field semantics, and [`docs/abi/versioning.md`](../docs/abi/versioning.md)
  for the compatibility policy that governs schema bumps.
- `examples/helloapp.json` — allow manifest used by the HelloApp slice
  (#82). Grants `CAP_CONSOLE_WRITE` at launch.
- `examples/helloapp.deny.json` — deny manifest counterpart. The app
  must handle `OS_STATUS_DENIED` gracefully (capability marked
  `optional`).
- `task-dag.schema.json` / `task-dag.example.json` — agent task DAG
  schema, unrelated to the app manifest above. See
  [`docs/test-plans/task-schema.md`](../docs/test-plans/task-schema.md).

## Wiring

Schema validation runs on every PR via `.github/workflows/lint.yml`,
which invokes `build/scripts/validate_manifests.sh`. The wrapper shells
into `tools/validate_manifests.py`, which in turn validates every
`manifests/examples/*.json` (and any other `*.manifest.json` in the
tree) against `manifests/schema/v0.json` using `jsonschema`.

To run the same check locally:

```sh
build/scripts/validate_manifests.sh        # bash
build/scripts/validate_manifests.ps1       # PowerShell (parity peer, #156)
```

Issue #183 landed the schema and examples; #187 wired the example into
the tree; #195 wired this validator into CI.
