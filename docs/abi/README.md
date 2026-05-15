# SecureOS ABI Reference

This directory is the canonical, append-only reference for the externally-visible
SecureOS interfaces. It exists to make ABI drift auditable while we are still at
`OS_ABI_VERSION=0` (rapid iteration), so that the freeze to `1` at SDK beta
(see `BUILD_ROADMAP.md` §7) can be performed against a known, written contract
rather than against whatever the source tree happened to look like at freeze
time.

## Scope

These documents describe the surface that user-space code (apps, libraries,
external SDK consumers) is allowed to depend on:

| Doc                                | Surface                                                      |
| ---------------------------------- | ------------------------------------------------------------ |
| [`syscalls.md`](./syscalls.md)     | The `os_*` user-facing API and required capability per call. |
| [`capabilities.md`](./capabilities.md) | Capability ID list, handle representation, grant/revoke/audit semantics. |
| [`manifest.md`](./manifest.md)     | The launcher / app manifest schema and signing requirements. |
| [`versioning.md`](./versioning.md) | `OS_ABI_VERSION` policy, freeze plan, compatibility shim window. |

Anything not described here is **not** part of the ABI and may change without
notice (kernel internals, `_for_tests` symbols, validator script names,
artifact layouts inside `artifacts/`, etc.).

## "Last verified against commit" discipline

Each ABI doc carries a `Last verified against commit:` line at the bottom.
When you change one of the underlying surfaces (add a syscall, add a capability
ID, change manifest fields, etc.) you **must** update the corresponding doc
*and* bump its `Last verified` line in the same commit. The "M-end checklist"
in `BUILD_ROADMAP.md` should re-verify each doc at the close of every
milestone (M2, M3, M4, …) and refresh the line even when nothing changed.

This is the cheapest possible drift detector: if a reviewer sees a syscall or
capability ID added without a docs touch, the PR is incomplete.

## Stability rules at a glance

- Capability numeric IDs are **append-only** and never renumbered (see
  `docs/architecture/CAPABILITIES.md` CAP-001 contract).
- Syscall names and signatures in `user/include/secureos_api.h` are part of
  the ABI surface; renaming or changing argument order is a breaking change
  and requires `OS_ABI_VERSION` bump + compat shim per `versioning.md`.
- Manifest field names are part of the ABI; field additions are
  backward-compatible if defaulted, removals or renames are breaking.
- Result codes (`os_status_t`, `cap_result_t`, `sof_result_t`) are
  append-only; existing numeric values never change.

## Cross-references

- `docs/architecture/CAPABILITIES.md` — narrative of the capability core
  (CAP-001 .. CAP-019), kept as the engineering history. `capabilities.md`
  here is the API-reference distillation for SDK consumers.
- `BUILD_ROADMAP.md` §7 — overarching ABI freeze policy.
- `kernel/format/sof.h` — the SOF (Signed Object Format) container that
  signed bundles, libraries, and OS commands are wrapped in. The on-wire
  bytes of SOF are an ABI concern; see `manifest.md`.

Last verified against commit: `9f4f7cc` (2026-05-15).
