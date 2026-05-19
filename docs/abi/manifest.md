# Module Manifest Schema and Compatibility Policy

> **Owner:** unassigned
> **Status:** draft
> **Last reviewed:** 2026-05-19
> **Applies to:** `OS_ABI_VERSION = 0`

This document defines **manifest schema v0** — the declarative contract a
SecureOS module ships alongside its signed binary so the launcher and the
capability broker can decide, before the module runs, what it is allowed to
do and what it offers to other modules.

Scope tracked by **#183** (BUILD_ROADMAP §5.2 M2, §5.6 M6, §7).

The schema is intentionally minimal: only fields the launcher / broker need
in order to satisfy §5.2 ("launcher module with manifest-based cap grants")
and §5.6 ("manifest capability declarations enforced by launcher / broker")
are normative. Fields outside this document are **not** part of v0 and MUST
be ignored by conformant launchers.

---

## 1. File format

A manifest is a **JSON** document. JSON was chosen over TOML for v0 because:

1. The in-tree validator stack (`build/scripts/*`, `tools/validate_bundle.sh`,
   the run-bundle layout in #161) already speaks JSON exclusively, so there
   is no second parser to vendor into the kernel image.
2. The IPC wire format draft (#180) is expected to use a length-prefixed JSON
   envelope; sharing a parser keeps the trusted surface small.
3. A machine-readable JSON-Schema validator (see §6) drops in directly.

Manifest filename convention: `<module-name>.manifest.json`, stored next to
the signed binary in the module bundle and in `manifests/examples/` for
in-tree fixtures.

Encoding: UTF-8, no BOM, LF line endings (matches repository policy in
AGENTS.md §"deterministic build").

---

## 2. Top-level shape (v0)

```json
{
  "os_abi_version": 0,
  "manifest_version": 0,
  "identity": { ... },
  "requested_capabilities": [ ... ],
  "provided_services": [ ... ],
  "signature": { ... }
}
```

All five top-level fields are **required** in v0. Unknown top-level fields
MUST cause the launcher to reject the manifest (fail-closed; cf.
`capability-handle.md` deny-path contract in #164).

### 2.1 `os_abi_version` (integer, required)

MUST equal the `OS_ABI_VERSION` constant the module was built against
(see #150 for the canonical header). The launcher refuses to load a module
whose `os_abi_version` does not match the running kernel's
`OS_ABI_VERSION`, per the §7 freeze policy.

For v0 this value is exactly `0`.

### 2.2 `manifest_version` (integer, required)

The schema revision this document describes. For v0 this value is exactly
`0`. See §7 below for the migration policy.

`manifest_version` is **distinct** from `os_abi_version`: the manifest
schema can evolve faster than the syscall / IPC ABI (additive field
additions), and bumping one does not force the other.

### 2.3 `identity` (object, required)

```json
"identity": {
  "name": "helloapp",
  "version": "0.1.0",
  "signer_key_id": "ed25519:da39a3ee5e6b4b0d3255bfef95601890afd80709"
}
```

| Field           | Type   | Required | Notes                                                                            |
| --------------- | ------ | -------- | -------------------------------------------------------------------------------- |
| `name`          | string | yes      | `[a-z][a-z0-9_-]{0,31}`. Unique within a bundle.                                 |
| `version`       | string | yes      | SemVer 2.0.0 (`MAJOR.MINOR.PATCH`).                                              |
| `signer_key_id` | string | yes      | `<alg>:<hex-fingerprint>`. v0 only defines `ed25519:` per #133 / #137 / #138.    |

The launcher MUST refuse to load two modules with the same `identity.name`
in the same bundle (fail-closed).

### 2.4 `requested_capabilities` (array, required)

Each element is an object naming one capability the module wants the broker
to grant on its behalf. May be empty.

```json
"requested_capabilities": [
  { "id": "CAP_CONSOLE_WRITE", "required": true,  "reason": "user-facing output" },
  { "id": "CAP_FS_READ",       "required": false, "reason": "optional config load" }
]
```

| Field      | Type    | Required | Notes                                                                              |
| ---------- | ------- | -------- | ---------------------------------------------------------------------------------- |
| `id`       | string  | yes      | Symbolic name from `kernel/cap/capability.h` (e.g. `CAP_CONSOLE_WRITE`, see §3).   |
| `required` | boolean | yes      | If `true` and the broker denies the grant, the launcher MUST refuse to start it.   |
| `reason`   | string  | no       | Free-text rationale, surfaced in audit logs (#84 / #164 deny-path).                |

Numeric capability IDs (the integer values of the `capability_id_t` enum)
are **not** allowed in the manifest. The symbolic name is the stable
contract surface; the integer is an implementation detail of the in-tree
header and may shift across `OS_ABI_VERSION` bumps. The broker resolves
symbolic name → integer at load time and emits a structured deny line if
the name is unknown.

### 2.5 `provided_services` (array, required)

Endpoints the module is willing to serve to other modules over IPC. May be
empty.

```json
"provided_services": [
  { "name": "console.write", "ipc_endpoint": "/svc/console/write", "stability": "stable" }
]
```

| Field          | Type   | Required | Notes                                                          |
| -------------- | ------ | -------- | -------------------------------------------------------------- |
| `name`         | string | yes      | Dotted lowercase identifier, e.g. `console.write`.             |
| `ipc_endpoint` | string | yes      | Path-like identifier consumed by the IPC layer (#180).         |
| `stability`    | string | yes      | One of `experimental`, `stable`, `deprecated`.                 |

The IPC wire format for these endpoints is defined in
[`ipc-wire.md`](./ipc-wire.md) (#180) — the manifest only advertises which
endpoints exist, not their argument shapes.

### 2.6 `signature` (object, required)

```json
"signature": {
  "alg": "ed25519",
  "binary_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
  "manifest_sha256": "<sha256 of this manifest with the `signature` block set to {}>",
  "sig": "<base16 ed25519 signature over (manifest_sha256 || binary_sha256)>"
}
```

The signature covers both the manifest bytes (canonicalised by zeroing the
`signature` block) **and** the signed binary's SHA-256, preventing
mix-and-match attacks where a valid manifest is paired with a different
binary.

The verifying key is identified by `identity.signer_key_id` and resolved
against the keyring established by #133 (ed25519 fix) / #138 (verify at
rest) / #137 (RFC 8032 KAT).

---

## 3. Capability vocabulary (v0)

The capability symbolic names recognised by the v0 broker correspond
one-to-one with the `capability_id_t` enum in
[`kernel/cap/capability.h`](../../kernel/cap/capability.h):

| Symbolic name           | Notes                                                  |
| ----------------------- | ------------------------------------------------------ |
| `CAP_CONSOLE_WRITE`     | Write to the framebuffer / serial console (#81 / #92). |
| `CAP_SERIAL_WRITE`      | Raw serial port write.                                 |
| `CAP_DEBUG_EXIT`        | QEMU debug-exit (test harness only).                   |
| `CAP_CAPABILITY_ADMIN`  | Broker-administered grants (highly restricted).        |
| `CAP_DISK_IO_REQUEST`   | Issue disk I/O requests via fs_service (#83).          |
| `CAP_FS_READ`           | Read mediated by `fs_service` (#83 / #108).            |
| `CAP_FS_WRITE`          | Write mediated by `fs_service`.                        |
| `CAP_EVENT_SUBSCRIBE`   | Subscribe to broker event stream.                      |
| `CAP_EVENT_PUBLISH`     | Publish events to the broker event stream.             |
| `CAP_APP_EXEC`          | Spawn additional modules via the launcher.             |
| `CAP_CODESIGN_BYPASS`   | Reserved; never granted outside the test harness.      |
| `CAP_NETWORK`           | Use `netlib` (#79 scheme gate applies).                |

Additions to this vocabulary land via PRs that update **both** the kernel
header and this table (and bump `manifest_version` if the field semantics
change — see §7).

---

## 4. Required field summary

The launcher MUST reject a manifest that fails any of the following checks
(fail-closed; deny-path logged per #164):

1. JSON parse error.
2. Missing or wrong-type top-level field.
3. Unknown top-level field.
4. `os_abi_version` mismatches the kernel's `OS_ABI_VERSION`.
5. `manifest_version` is unrecognised by the running launcher.
6. `identity.name` does not match `[a-z][a-z0-9_-]{0,31}`.
7. `identity.version` is not valid SemVer 2.0.0.
8. `identity.signer_key_id` is not `<alg>:<hex>` with a known `<alg>`.
9. Any `requested_capabilities[].id` is not in the §3 vocabulary.
10. Any `provided_services[].stability` is outside the enum.
11. Signature verification fails (per §2.6 / #133).

---

## 5. Example: HelloApp

A complete in-tree example lives at
[`manifests/examples/helloapp.manifest.json`](../../manifests/examples/helloapp.manifest.json).
It is the canonical reference for #92 (HelloApp console-write deny-path
tests) and the M6 SDK template (#136).

---

## 6. Machine-readable schema

A JSON-Schema (draft 2020-12) validator stub lives at
[`manifests/schema/v0.json`](../../manifests/schema/v0.json).

It is intentionally **not** wired into CI in this issue (per #183 scope —
schema lands here, CI integration is a follow-up). Authors can validate
locally with any draft 2020-12 compliant validator, e.g.:

```bash
# example only; ajv-cli or check-jsonschema both work
check-jsonschema --schemafile manifests/schema/v0.json \
                 manifests/examples/helloapp.manifest.json
```

A follow-up issue will wire this validator into `build/scripts/test.sh`
once it is part of the deterministic build toolchain.

---

## 7. Compatibility policy

The manifest schema follows the same versioning discipline as the rest of
the ABI surfaces (BUILD_ROADMAP §7), but with one extra dimension:

| Change kind                                                              | Action                                                        |
| ------------------------------------------------------------------------ | ------------------------------------------------------------- |
| Add a new optional top-level field                                       | Bump `manifest_version`. `OS_ABI_VERSION` unchanged.          |
| Add a new capability symbol to §3                                        | No version bump (vocabulary is open-ended; readers ignore unknown ids only if `required = false`). |
| Change the meaning of an existing field, or remove / rename a field      | Bump `manifest_version` **and** `OS_ABI_VERSION`.             |
| Change the signature algorithm or canonicalisation rule (§2.6)           | Bump `manifest_version` **and** `OS_ABI_VERSION`.             |

During the §7 pre-freeze window (`OS_ABI_VERSION = 0`), breaking changes
are allowed but MUST update this document, bump `manifest_version`, and
ship a migration note in `BUILD_ROADMAP.md` §7.

After SDK beta freeze (`OS_ABI_VERSION ≥ 1`), at least one prior
`manifest_version` MUST keep working through compatibility shims for one
major version, per §7.

A launcher running ABI version `N` MUST accept any
`manifest_version ≤ N`. A launcher MUST refuse a higher `manifest_version`
(fail-closed).

---

## 8. Out of scope for v0

The following are deliberately deferred and will be tracked by follow-up
issues (not by #183):

- Wiring the JSON-Schema validator into CI / `build/scripts/test.sh`.
- Launcher implementation of these checks (that is the execute issue under
  #82 / #87 / #100).
- Manifest support for multi-bundle (system-wide) deployments.
- A binary / canonical-CBOR encoding of the manifest. v0 is JSON-only.
- Quota / resource-limit fields (CPU, memory, fd count). These will be a
  separate v1 surface once #115 (broker tests) lands.
- IPC-endpoint argument schemas — owned by [`ipc-wire.md`](./ipc-wire.md)
  (#180), not by this document.
