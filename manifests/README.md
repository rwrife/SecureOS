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

Issue #183 lands the schema and examples without CI enforcement. Wiring
schema validation into CI is the follow-up tracked in #195.
