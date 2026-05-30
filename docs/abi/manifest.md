# SecureOS Launcher Manifest

> **Owner:** launcher / user-runtime
> **Status:** draft `v0` — on-disk schema not yet load-bearing
> **Last reviewed:** 2026-05-29
> **Applies to:** `OS_ABI_VERSION = 0`
> **Tracking issues:** [#82](https://github.com/rwrife/SecureOS/issues/82), [#83](https://github.com/rwrife/SecureOS/issues/83)

At this milestone, the "manifest" is the in-code launcher registration
API plus the explicit grant calls it exposes. The on-disk JSON form is
sketched below but not yet load-bearing; it is the target shape we will
freeze once the launcher-mediated console slice (#82) and filesystem
slice (#83) close.

The launcher is the **only** sanctioned path that widens an app's
capability set. The kernel capability gates do the enforcement, but the
launcher decides whether a given app subject ever gets a grant in the
first place.

## In-code surface (today)

From [`kernel/user/launcher.h`](../../kernel/user/launcher.h):

- `launcher_register_app(app_id, subject_id)` — register an app subject
  with the launcher. Apps not registered cannot be granted any capability
  through the launcher.
- `launcher_grant_console_write(app_id)` /
  `launcher_revoke_console_write(app_id)` — the only sanctioned path to
  widen / narrow `CAP_CONSOLE_WRITE`.
- `launcher_app_console_write(app_id, msg, *bytes_written)` — single app
  output entrypoint; routes through `cap_console_write_gate` so
  deny-by-default still holds.
- `launcher_app_has_console_write(app_id)` — read-only inspection; never
  widens access.

A non-launcher subject that calls the underlying gate directly without
its own explicit grant is denied. The bypass-regression test in
`tests/launcher_console_test.c` proves this.

## Manifest schema v0

The machine-readable schema lives at
[`manifests/schema/v0.json`](../../manifests/schema/v0.json) (JSON Schema
draft 2020-12). Worked examples consumed by the HelloApp slice (#82)
live under [`manifests/examples/`](../../manifests/examples/) —
`helloapp.json` (allow) and `helloapp.deny.json` (deny).

JSON was chosen over TOML for v0 because:

1. The repo already ships JSON Schema for the task DAG
   (`manifests/task-dag.schema.json`), so validators and tooling are
   already pulled in.
2. The on-image launcher parser stays trivial — no TOML library has to
   land in the kernel-adjacent code path.
3. "Unknown field = error" is straightforward to enforce with
   `additionalProperties: false`, which matches the zero-trust posture.

A representative document:

```jsonc
{
  "manifest_version": 0,
  "os_abi_version": 0,
  "app": {
    "id": "helloapp",
    "version": "0.1.0",
    "subject_id": 2,
    "binary": "apps/helloapp.bin",
    "signer_key_id": "secureos-dev-key-1"
  },
  "capabilities": {
    "request": ["CAP_CONSOLE_WRITE"],
    "optional": []
  },
  "provides": [],
  "launcher": {
    "auto_grant_at_launch": ["CAP_CONSOLE_WRITE"],
    "require_user_confirm": []
  },
  "signature": {
    "algorithm": "ed25519",
    "signer_key_id": "secureos-dev-key-1",
    "signature_path": "apps/helloapp.bin.sig"
  }
}
```

Field semantics we are committing to at v0:

- `manifest_version` is required, must equal `0`, and is independent of
  `os_abi_version` (the schema can iterate inside an ABI family).
- `os_abi_version` must equal the kernel's `OS_ABI_VERSION` constant
  (issue #150). Mismatches MUST be rejected by the launcher; this is the
  hand-shake that ties the manifest to the ABI surface frozen in
  [`versioning.md`](./versioning.md).
- Unknown top-level or nested fields are an error, not a warning. This
  matches `additionalProperties: false` in the schema — zero-trust
  applies to manifest parsing too.
- `app.id` is a stable identifier (`^[a-z0-9][a-z0-9_-]{0,62}$`).
- `app.subject_id` is the `cap_subject_id_t` the launcher registers for
  this app. It must be unique across loaded apps and fit inside
  `CAP_TABLE_MAX_SUBJECTS = 8` (see
  [`capabilities.md`](./capabilities.md)).
- `capabilities.request` enumerates the capabilities the app *may* use.
  The launcher will refuse to grant anything not listed here, even if
  `launcher.auto_grant_at_launch` mentions it.
- `capabilities.optional` MUST be a subset of `capabilities.request`.
  Apps that list a capability here MUST check the `OS_STATUS_DENIED`
  result and degrade gracefully (see
  [`capability-deny-contract.md`](./capability-deny-contract.md)).
- `capabilities.persistence` is an optional enum (`"ephemeral"` |
  `"persistent"`) that declares the FS persistence mode the app
  requests from `launcher_fs` (BUILD_ROADMAP §5.3, issue #285).
  The default is `"ephemeral"`: writes go to the faux FS and are
  wiped at `process_destroy` / relaunch. `"persistent"` is a
  *request* only — it is granted at spawn time **only** if
  `launcher_fs` policy approves `CAP_FS_WRITE` for this subject;
  apps MUST tolerate being downgraded to ephemeral the same way
  they tolerate `OS_STATUS_DENIED` on an `optional` capability.
  Adding the field is schema-only at v0 — the launcher_fs wiring
  lands in issue #279 — so this is an additive, v0-compatible
  change and does **not** bump `OS_ABI_VERSION` (see Compatibility
  policy below).
- `capabilities.broker_role` is an optional enum
  (`"provider"` | `"consumer"` | `"none"`) that declares whether
  this app participates in the broker-share contracts named in
  BUILD_ROADMAP §5.4 (`DocumentProvider` / `AttachmentConsumer`,
  see issue #312 and plan #299). The default is `"none"`, which
  preserves today's behavior exactly: the launcher hands no broker
  handle to the app at spawn time and `ipc_scratch[24..31]` is left
  as it is in the pre-broker spawn path. `"provider"` marks an app
  that exposes shareable handles via `broker_svc`; `"consumer"`
  marks an app that expects to receive a broker-issued handle in
  `ipc_scratch[24..31]` on spawn. Like `persistence`, this field is
  schema-only at v0 — the launcher and `broker_svc` wiring lands
  in a later slice (plan #299 follow-up). Because the field is
  **optional**, has a non-`"none"` value only when explicitly
  declared, and `"none"` exactly preserves the current spawn path,
  adding it is additive and does **not** bump `OS_ABI_VERSION`
  (same precedent as `capabilities.persistence` in #285/#286).
- `capabilities.ownership_role` is an optional enum
  (`"owner"` | `"delegate"` | `"none"`) that declares what role this
  app plays in the M5 ownership graph named in BUILD_ROADMAP §5.5
  (see issue #368 and plan #313). The default is `"none"`, which
  preserves today's behavior exactly: the launcher registers no
  ownership edge for the subject and `BROKER_OP_DELETE_OWNER` will
  not cascade through it. `"owner"` marks an app that is registered
  as an owner node — its minted handles cascade-revoke when the
  owner is deleted (see `tests/broker_svc_cascade_revokes_minted_handle_test.c`
  and the substrate-tier `m5_owner_delete_cascade_allow_qemu` peer).
  `"delegate"` marks an app that receives delegated handles from a
  parent owner; deleting the parent invalidates these per §5.5
  (`:delegated_caps_invalid` sub-check). Like `persistence` and
  `broker_role`, this field is schema-only at v0 — the launcher and
  broker_svc runtime wiring lands in later M5-SUBSTRATE slices.
  Because the field is **optional**, has a non-`"none"` value only
  when explicitly declared, and `"none"` exactly preserves the
  current spawn / ownership-edge path, adding it is additive and
  does **not** bump `OS_ABI_VERSION` (same precedent as
  `capabilities.persistence` in #285/#286 and
  `capabilities.broker_role` in #312).
- `provides[]` lists IPC endpoints the app exposes (see
  [`ipc-wire.md`](./ipc-wire.md)). Endpoint names use a narrow
  reverse-DNS-ish pattern.
- `owner.kind` is an optional enum (`"internal"` | `"external"`)
  that marks the origin of the module (BUILD_ROADMAP §5.6, plan
  `plans/2026-05-15-public-sdk-external-app-template.md`
  §"Manifest Schema (additions)", issue #396). The default is
  `"internal"`, which preserves today's behavior exactly: the
  module is shipped in-tree and the launcher applies its existing
  trust/policy path unchanged. `"external"` marks a module produced
  via the public SDK toolchain (`os-cc` / `os-pack`) by a
  third-party author; downstream M6 slices may use this marker
  to apply stricter signing or cap-grant policy. Like
  `capabilities.persistence`, `capabilities.broker_role`, and
  `capabilities.ownership_role`, this field is schema-only at v0 —
  the runtime wiring (M6-SDK-003 wrappers and the
  `sdk_external_build_isolation` acceptance test) lands in later
  M6-SDK slices and is gated on the A/B design decision tracked
  in #396. Because the field is **optional**, has a non-default
  value only when explicitly declared, and `"internal"` exactly
  preserves the current launcher path, adding it is additive and
  does **not** bump `OS_ABI_VERSION` (same precedent as
  `capabilities.persistence` in #285/#286,
  `capabilities.broker_role` in #312, and
  `capabilities.ownership_role` in #368, and `owner.kind` in #396).
- `runtime.arena_bytes` is an optional integer field that declares a
  per-app userland arena ceiling in bytes (BUILD_ROADMAP §5.7 and
  plan `plans/2026-05-28-in-os-toolchain-self-hosting.md` §P1,
  issue #424). The accepted range at v0 is
  `[PROC_ARENA_BYTES_DEFAULT = 65536, PROC_ARENA_BYTES_MAX = 16777216]`
  (64 KiB … 16 MiB). Numbers chosen: the default is a small, safe
  bootstrap-app footprint that costs no extra physical pages over
  today's behavior; the cap is sized to fit the in-OS TinyCC
  compile-with-linker working set (plan §P1) while staying well below
  the M1 process address-space envelope, so a per-app value cannot
  silently consume the whole arena window. Default behavior when the
  `runtime` object (or `runtime.arena_bytes`) is omitted is
  **unchanged**: the launcher applies the kernel default arena size,
  exactly matching the pre-#424 spawn path. A declared value that
  exceeds the cap is a deny-by-default launch failure with a
  documented audit reason — the launcher emits a capability-audit
  deny event and the spawn fails; the kernel does **not** panic. Like
  `persistence`, `broker_role`, `ownership_role`, and `owner.kind`,
  this field is schema-only at v0 — the kernel `os_mem_brk` syscall
  and the launcher clamp/audit wiring land in the M7-TOOLCHAIN-001
  follow-up slice (#421). Because the field is **optional**, has a
  non-default value only when explicitly declared, and the omitted
  case exactly preserves the current spawn / arena-sizing path,
  adding it is additive and does **not** bump `OS_ABI_VERSION`
  (same precedent as `capabilities.persistence` in #285/#286,
  `capabilities.broker_role` in #312,
  `capabilities.ownership_role` in #368, and `owner.kind` in #396).
- `launcher.auto_grant_at_launch` is the subset the launcher grants
  unconditionally at startup. It MUST be disjoint from
  `launcher.require_user_confirm`; both MUST be subsets of
  `capabilities.request`.
- `signature` is required for any non-bootstrap app once `CAP_APP_EXEC`
  is enforced end-to-end (post-#133 chain). At v0 it is permitted to be
  omitted only for unsigned bootstrap apps under `CAP_CODESIGN_BYPASS`
  in sealed-build mode.

Validation of these constraints that go beyond pure JSON Schema (subset
relationships, disjointness, unique subject ids across an image) is the
launcher's responsibility and is tracked under #82 / #195. This issue
deliberately does not wire the schema into CI; that is the follow-up in
#195.

## Worked example: HelloApp (M2)

The HelloApp slice (#82) registers a subject with the launcher, requests
`CAP_CONSOLE_WRITE`, and is exercised under two manifests, both checked
into [`manifests/examples/`](../../manifests/examples/):

- **Allow** — [`helloapp.json`](../../manifests/examples/helloapp.json):
  `auto_grant_at_launch: ["CAP_CONSOLE_WRITE"]`. The app prints its
  banner; the validator emits
  `TEST:PASS:helloapp_console_write_allowed`.
- **Deny** — [`helloapp.deny.json`](../../manifests/examples/helloapp.deny.json):
  `auto_grant_at_launch: []`, with `CAP_CONSOLE_WRITE` marked
  `optional` so the app is required to handle denial gracefully. The
  app's `os_console_write` returns `OS_STATUS_DENIED`; the validator
  emits `TEST:PASS:helloapp_denied_console_write` and a
  capability-audit deny event is recorded (see #92 for the dedicated
  negative-path test and #84 for the audit assertion).

Two additional fixtures exercise the optional
`capabilities.broker_role` enum added in #312:

- **Broker provider** —
  [`helloapp.broker_provider.json`](../../manifests/examples/helloapp.broker_provider.json):
  same as `helloapp.json` but with `capabilities.broker_role:
  "provider"`. Demonstrates a `DocumentProvider` declaration.
- **Broker consumer** —
  [`helloapp.broker_consumer.json`](../../manifests/examples/helloapp.broker_consumer.json):
  same as `helloapp.json` but with `capabilities.broker_role:
  "consumer"`. Demonstrates an `AttachmentConsumer` declaration.

Both fixtures are schema-only at v0: the launcher and `broker_svc`
still behave as they do today (no broker handle is handed in on
spawn) — the field's runtime meaning is wired up in a later slice
(plan #299 follow-up). The existing `helloapp.json` /
`helloapp.deny.json` / `helloapp.persistent.json` examples omit
`broker_role` and so continue to behave as `broker_role: "none"`,
proving that the addition is backward-compatible.

Two additional fixtures exercise the optional
`capabilities.ownership_role` enum added in #368:

- **Ownership owner** —
  [`helloapp.ownership_owner.json`](../../manifests/examples/helloapp.ownership_owner.json):
  same as `helloapp.json` but with `capabilities.ownership_role:
  "owner"`. Demonstrates an M5 owner-node declaration.
- **Ownership delegate** —
  [`helloapp.ownership_delegate.json`](../../manifests/examples/helloapp.ownership_delegate.json):
  same as `helloapp.json` but with `capabilities.ownership_role:
  "delegate"`. Demonstrates a delegated-handle declaration.

These fixtures are schema-only at v0: the launcher still registers
no ownership edge for the subject at spawn (today's behavior) —
the field's runtime meaning is wired up in later M5-SUBSTRATE
slices. The pre-existing `helloapp.json` / `helloapp.deny.json` /
`helloapp.persistent.json` / `helloapp.broker_*.json` examples omit
`ownership_role` and so continue to behave as
`ownership_role: "none"`, proving that the addition is backward-
compatible.

Negative-path coverage matching the §5.3 (`manifest_persistence_enum`)
and §5.4 (`manifest_broker_role_enum`) precedents lands a checked-in
bad fixture and an in-test synthesized variant:

- **Out-of-enum (rejected)** —
  [`invalid/helloapp.ownership_invalid.json`](../../manifests/examples/invalid/helloapp.ownership_invalid.json):
  same as `helloapp.json` but with `capabilities.ownership_role:
  "supervisor"` (not in the v0 enum). The validator wrapper rejects
  it with a deterministic `MANIFEST_VALIDATE:(ERROR|FAIL)` marker
  surfacing the `ownership_role` field name. The bundled regression
  test `build/scripts/test_manifest_ownership_role_enum.sh` emits the
  `TEST:PASS:manifest_ownership_role_enum:negative_rejected` sub-marker
  on success (parity with `manifest_persistence_enum:negative_rejected`
  / `manifest_broker_role_enum:negative_rejected`).
- **Default when omitted** — the same regression test emits
  `TEST:PASS:manifest_ownership_role_enum:default_when_omitted` after
  asserting that `helloapp.json` (which omits `ownership_role` entirely)
  still validates, locking in the additive / back-compat contract
  (parity with the matching §5.3 / §5.4 sub-marker spellings).

The `invalid/` subdirectory is deliberately outside the
`manifests/examples/*.json` glob walked by `validate_manifests.sh`, so
the checked-in bad fixture does not show up in the bulk-validator pass
list while still being a stable target for the regression test.

### §5.5 ownership_role enforcement status

| Sub-marker | Status |
| --- | --- |
| `manifest_ownership_role_enum` (positive) | enforced (PR #372) |
| `manifest_ownership_role_enum:negative_rejected` | enforced (PR for #390) |
| `manifest_ownership_role_enum:default_when_omitted` | enforced (PR for #390) |

### §5.6 owner.kind enforcement status (M6-SDK-003 schema sub-slice)

`owner.kind` enum added in #396 (M6 SDK external-app marker):

- **Owner external** —
  [`helloapp.owner_external.json`](../../manifests/examples/helloapp.owner_external.json):
  same as `helloapp.json` but with `owner.kind: "external"`.
  Demonstrates an SDK-produced (third-party) module declaration.
- **Owner internal** —
  [`helloapp.owner_internal.json`](../../manifests/examples/helloapp.owner_internal.json):
  same as `helloapp.json` but with `owner.kind: "internal"`
  (explicit form of today's default; behaves identically to
  omitting the field).
- **Owner omitted (back-compat)** — the bundled
  `helloapp.json`, `helloapp.deny.json`, `helloapp.persistent.json`,
  `helloapp.broker_provider.json`, `helloapp.broker_consumer.json`,
  `helloapp.ownership_owner.json`, and
  `helloapp.ownership_delegate.json` examples all omit the
  `owner` object entirely and so continue to behave as
  `owner.kind: "internal"`, proving that the addition is
  backward-compatible.
- **Negative regression** —
  [`invalid/helloapp.owner_kind_invalid.json`](../../manifests/examples/invalid/helloapp.owner_kind_invalid.json):
  same as `helloapp.json` but with `owner.kind: "vendor"`, which
  is rejected by `tools/validate_manifests.py` with a
  deterministic `MANIFEST_VALIDATE:FAIL` marker surfacing the
  `/owner/kind` field path. The bundled regression test
  `build/scripts/test_manifest_owner_kind_enum.sh` emits the
  `TEST:PASS:manifest_owner_kind_enum:negative_rejected` sub-marker
  on its success path (parity with the matching
  `:negative_rejected` sub-markers in
  `manifest_persistence_enum`, `manifest_broker_role_enum`, and
  `manifest_ownership_role_enum`), and emits
  `TEST:PASS:manifest_owner_kind_enum:default_when_omitted` after
  asserting that `helloapp.json` (which omits the `owner` object
  entirely) still validates, locking in the additive / back-compat
  contract.

| Sub-marker | Status |
| --- | --- |
| `manifest_owner_kind_enum` (positive) | enforced (PR for #396 schema sub-slice) |
| `manifest_owner_kind_enum:negative_rejected` | enforced (PR for #396 schema sub-slice) |
| `manifest_owner_kind_enum:default_when_omitted` | enforced (PR for #396 schema sub-slice) |

### §5.7 runtime.arena_bytes enforcement status (M7-TOOLCHAIN-001 schema sub-slice)

`runtime.arena_bytes` additive integer field added in #424 (M7
per-app userland arena ceiling, refs #404 / #421):

- **Runtime arena example** —
  [`helloapp.runtime_arena.json`](../../manifests/examples/helloapp.runtime_arena.json):
  same as `helloapp.json` but with `runtime.arena_bytes: 1048576`
  (1 MiB, a representative mid-range value).
- **Runtime omitted (back-compat)** — the bundled `helloapp.json`,
  `helloapp.deny.json`, `helloapp.persistent.json`,
  `helloapp.broker_*.json`, `helloapp.ownership_*.json`, and
  `helloapp.owner_*.json` examples all omit the `runtime` object
  entirely and so continue to behave as the kernel-default arena
  size, proving that the addition is backward-compatible.
- **Negative regression** —
  [`invalid/helloapp.runtime_arena_invalid.json`](../../manifests/examples/invalid/helloapp.runtime_arena_invalid.json):
  same as `helloapp.json` but with `runtime.arena_bytes: 33554432`
  (32 MiB, above the v0 cap of 16 MiB). The validator wrapper
  rejects it with a deterministic `MANIFEST_VALIDATE:FAIL` marker
  surfacing the `arena_bytes` field name. The bundled regression
  test `build/scripts/test_manifest_arena_bytes_range.sh` also
  exercises in-test synthesized negatives (below-min, negative
  integer, wrong type) and emits the
  `TEST:PASS:manifest_arena_bytes_range:negative_rejected` sub-marker
  on its success path (parity with the matching `:negative_rejected`
  sub-markers in `manifest_persistence_enum`,
  `manifest_broker_role_enum`, `manifest_ownership_role_enum`, and
  `manifest_owner_kind_enum`), and emits
  `TEST:PASS:manifest_arena_bytes_range:default_when_omitted` after
  asserting that `helloapp.json` (which omits the `runtime` object
  entirely) still validates, locking in the additive / back-compat
  contract.

| Sub-marker | Status |
| --- | --- |
| `manifest_arena_bytes_range` (positive) | enforced (PR for #424 schema sub-slice) |
| `manifest_arena_bytes_range:negative_rejected` | enforced (PR for #424 schema sub-slice) |
| `manifest_arena_bytes_range:default_when_omitted` | enforced (PR for #424 schema sub-slice) |
| `launcher_arena_bytes:default_when_omitted_matches_legacy` | enforced at spawn (PR for #448 launcher slice) |
| `launcher_arena_bytes:declared_value_applied` | enforced at spawn (PR for #448 launcher slice) |
| `launcher_arena_bytes:over_cap_denied` | enforced at spawn (PR for #448 launcher slice) |
| `launcher_arena_bytes:under_floor_denied` | enforced at spawn (PR for #448 launcher slice) |

Runtime semantics: as of the M7-TOOLCHAIN-001 launcher slice (#448),
`launcher_spawn_app_from_manifest()` and its fs/broker siblings read
`runtime.arena_bytes` from the manifest, resolve `0`/omitted to
`PROC_ARENA_BYTES_DEFAULT` (exactly preserving the pre-#424 spawn
path), accept any value in `[PROC_ARENA_BYTES_DEFAULT,
PROC_ARENA_BYTES_MAX]` as the per-spawn cap, and deny-by-default any
out-of-range value with `LAUNCHER_ERR_INVALID_MANIFEST` plus a
launcher-local audit cell
(`launcher_arena_last_deny_reason()` /
`launcher_arena_last_deny_value()` /
`launcher_arena_deny_count()`). The kernel does not panic on a
deny. The kernel `os_mem_brk` syscall (#421) continues to operate
against the existing `g_native_heap_pool` ceiling; threading the
per-spawn cap into the bridge so `os_mem_brk` returns
`OS_STATUS_DENIED` at the manifest-declared ceiling lands in the
follow-up wiring slice tracked by #404 (no schema change
required).

Runtime semantics (launcher trust/policy treatment of
`owner.kind: "external"` modules, plus the
`sdk_external_build_isolation` acceptance test) land in later
M6-SDK-003 sub-slices once the A/B design question in #396 is
resolved. This sub-slice intentionally introduces no runtime
behavior change.

## Compatibility policy

`manifest_version` and `os_abi_version` are two intentionally separate
knobs. `manifest_version` governs the *shape* of this document;
`os_abi_version` governs the kernel/launcher surface it talks to (see
[`versioning.md`](./versioning.md), issue #150).

Until `OS_ABI_VERSION` bumps to 1:

- Adding new optional manifest fields is allowed *only* by also bumping
  `manifest_version` — because v0 loaders are required to reject unknown
  fields (`additionalProperties: false`), behavior cannot silently drift
  in either direction.
- Adding new capability IDs to `request` is allowed without a version
  bump, subject to the rules in [`capabilities.md`](./capabilities.md);
  the schema constrains the identifier *shape*, not the enum of valid
  values.
- Tightening a constraint (e.g. narrowing a pattern, making an optional
  field required) requires a `manifest_version` bump.
- Renaming or removing any field requires a `manifest_version` bump.

When `OS_ABI_VERSION` itself moves to 1 (SDK beta freeze, per
[`versioning.md`](./versioning.md)):

- A new `manifests/schema/v1.json` is added alongside `v0.json` — v0 is
  retained for the rolling compat shim window.
- `manifest_version: 0` documents continue to load on `os_abi_version: 1`
  hosts *only* while the compat shim is in effect; the launcher emits a
  deprecation marker when it does.
- A `manifest_version: 1` document declaring `os_abi_version: 0` is
  always rejected (you cannot target a newer manifest shape at an older
  ABI host).

Last verified against commit: HEAD
