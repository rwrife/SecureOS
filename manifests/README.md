# manifests/

In-tree examples and the machine-readable schema for SecureOS module
manifests. The normative specification lives at
[`docs/abi/manifest.md`](../docs/abi/manifest.md) (issue #183).

## Layout

- `schema/v0.json` — JSON-Schema (draft 2020-12) validator for manifest v0.
  Not yet wired into CI (see follow-up note in `docs/abi/manifest.md` §6).
- `examples/helloapp.manifest.json` — reference manifest used by the
  HelloApp acceptance tests (#92) and the M6 SDK template (#136).

## Validating locally

```bash
# Either of these works:
check-jsonschema --schemafile manifests/schema/v0.json \
                 manifests/examples/helloapp.manifest.json

ajv validate -s manifests/schema/v0.json \
             -d manifests/examples/helloapp.manifest.json --spec=draft2020
```

## Status

Manifest schema is at `manifest_version = 0` / `OS_ABI_VERSION = 0`. The
field set, compatibility policy, and capability vocabulary may evolve until
SDK beta freeze (BUILD_ROADMAP §7); see `docs/abi/manifest.md` §7 for the
migration rules.
