# SecureOS Launcher Manifest

> **Owner:** launcher / user-runtime
> **Status:** draft `v0` — on-disk schema not yet load-bearing
> **Last reviewed:** 2026-05-22
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
