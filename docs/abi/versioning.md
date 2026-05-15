# SecureOS ABI Versioning Policy

This restates and operationalizes the policy in `BUILD_ROADMAP.md` §7
("ABI and Interface Freeze Plan"). It is the contract that
`OS_ABI_VERSION`-aware code (loader, manifest parser, future SDK build tools)
must follow.

## Current state

```
OS_ABI_VERSION = 0
```

All surfaces described in this directory (`syscalls.md`, `capabilities.md`,
`manifest.md`) are at version `0` — *rapid iteration*. Breaking changes are
permitted within this version but must:

- update the relevant doc in the same PR,
- bump that doc's `Last verified against commit` line, and
- carry a CHANGELOG-style entry in the PR body.

## Freeze plan

| Phase                              | `OS_ABI_VERSION` | Rules |
| ---------------------------------- | ---------------- | ----- |
| Pre-SDK (current; through M5)      | `0`              | Breaking changes allowed; surfaces stay documented. |
| SDK beta announced (M6 / #136)     | `1`              | Frozen. No breaking changes without bumping major. |
| Post-1.x maintenance               | `≥ 1`            | One major version of compatibility shims kept in tree. |

When the freeze to `1` happens (planned alongside M6 SDK work, #136), this
file's "Current state" block bumps to `1` and the docs in this directory
become contractual.

## Compatibility shim window

After a freeze:

- For one major version after a breaking change, the prior surface must
  remain callable via a shim. Concretely: if v2 removes
  `os_legacy_thing(...)`, the v2 build still ships an
  `os_legacy_thing(...)` symbol that delegates to or rejects with a
  documented `OS_STATUS_*` value.
- The compat shim is removed in v3 (one major after deprecation).
- Capability IDs are *never* removed; deprecated capabilities are documented
  as no-ops and still reserve their numeric ID.

## What is and is not part of the ABI

In-scope (versioned, audit-tracked here):

- The `os_*` syscall surface (`syscalls.md`).
- Capability IDs, result codes, and grant/revoke/audit semantics
  (`capabilities.md`).
- SOF container layout, manifest field names, and signing policy
  (`manifest.md`).
- `OS_STATUS_*`, `CAP_*` (result and operation enums), `SOF_*` enums —
  values are append-only.

Out of scope (may change at any time, not part of `OS_ABI_VERSION`):

- The `app_native_bridge_t` function-pointer table layout
  (an internal lowering detail; will be replaced by a real syscall
  instruction during M6 SDK work).
- `_for_tests` symbols in any kernel module.
- Kernel-internal data structures (cap table layout, audit ring storage,
  scheduler state).
- Validator script names and locations under `build/scripts/`.
- Layout of `artifacts/` and toolchain container internals.

## Changing this policy

Changes to the freeze plan or shim window must be reviewed against
`BUILD_ROADMAP.md` §7 and recorded as an ADR under `docs/adr/` (see
`docs/adr/0001-capability-core-boundary.md` for the pattern). This file is
the operational restatement; the roadmap is the source of truth for the
policy itself.

Last verified against commit: `9f4f7cc` (2026-05-15).
