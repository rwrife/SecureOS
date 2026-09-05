# Launch Audit Contract

Issue anchors: [#554](https://github.com/rwrife/SecureOS/issues/554), [#410](https://github.com/rwrife/SecureOS/issues/410)

This document is the authoritative ABI contract for launcher decision audit
markers that carry manifest ownership classification.

## 1. Marker shapes

- **Allow path**
  - `launch.granted:owner_kind=<internal|external|local>`
- **Deny path**
  - `launch.denied:owner_kind=<internal|external|local>:subject=<sid>:reason=<reason>`

### 1.1 Field spelling

The field name is `owner_kind=` (underscore form), not `owner.kind=`.
`owner.kind` is the manifest schema key; `owner_kind` is the launch-audit text
field exported for log scanners.

## 2. Owner-kind resolution rules

Launcher audit markers resolve owner kind from manifest metadata using the same
enum values documented in `docs/abi/manifest.md` §5.6:

- Manifest omits `owner` entirely → `owner_kind=internal`
- Manifest sets `owner.kind="internal"` → `owner_kind=internal`
- Manifest sets `owner.kind="external"` → `owner_kind=external`
- Manifest sets `owner.kind="local"` → `owner_kind=local`

Grant and deny records carry the same `owner_kind` value so forensic filters are
symmetric across allow/deny decisions.

## 3. Test coverage and rollout state

- Host gate (this issue): `launcher_owner_kind_audit_marker`
  - validates fixture coverage for `internal`, `external`, `local`, and
    default-when-omitted.
  - pins exact launch marker field text and the canonical shapes in
    `docs/abi/audit-markers.json`.
- Runtime QEMU gate (deferred consumer):
  `toolchain_launch_audit_owner_kind_field_emitted` remains SKIP-pinned until
  #410 wires the end-to-end unsigned-run launcher flow.

Last verified against commit: 7b426623acf7d630923c197337312801d7635289
