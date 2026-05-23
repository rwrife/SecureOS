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

Last verified against commit: 9b2089bbcfac9813eda86503e076f11f85ca4ab6
