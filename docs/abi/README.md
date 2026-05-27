# SecureOS ABI Reference

This directory is the authoritative reference for the SecureOS Application
Binary Interface (ABI) surface that user-space code, modules, and launcher
manifests depend on.

Per `BUILD_ROADMAP.md` §7 ("ABI and Interface Freeze Plan"), we publish ABI
docs early — while we are still at `OS_ABI_VERSION=0` — so that interface
changes are deliberate and reviewable rather than emergent.

## Index

- [syscalls.md](syscalls.md) — User-facing `os_*` syscall / API surface, the
  capability each call requires, and the documented error/status model.
- [capabilities.md](capabilities.md) — Capability identifiers, handle
  representation, grant/revoke semantics, admin-gate, non-delegability, and
  audit/sequence guarantees. Cross-references the deeper architecture notes
  in `docs/architecture/CAPABILITIES.md`.
- [capability-handle.md](capability-handle.md) — Normative `cap_handle_t`
  32-bit layout (`[slot:16 | gen:14 | tag:2]`), the
  `cap_gate_check_handle()` validation order, mint / revoke / bulk-revoke
  contracts, and the v0-reserved `cap_handle_revoke_subtree` symbol.
  Frozen at `OS_ABI_VERSION = 0` ([#233](https://github.com/rwrife/SecureOS/issues/233)).
- [manifest.md](manifest.md) — Launcher manifest schema: how an app declares
  the capabilities it needs and how the launcher mediates grants today.
- [ipc-wire.md](ipc-wire.md) — IPC wire format (`ipc_msg_v0`) + error
  model (`ipc_result_t`) for `OS_ABI_VERSION = 0`. Specified by #194;
  implementation tracked by the M1 sync-IPC plan (#180 / #185).
- [versioning.md](versioning.md) — `OS_ABI_VERSION` policy, compat-shim
  window, and the process for adding / removing ABI surface.
- [sosh-capability-contract.md](sosh-capability-contract.md) — capability
  surface + sandbox contract for the `sosh` scripting language: which
  builtins are side-effecting, which existing `CAP_*` gates them, how a
  script declares its requested caps, and the canonical deny marker.
  First slice of [#351](https://github.com/rwrife/SecureOS/issues/351).
- [capability-registry.md](capability-registry.md) — machine-readable index
  ([capability-registry.json](capability-registry.json)) of every `CAP_*`:
  numeric id, subject kinds, `CAP:DENY:` marker shape, allow/deny test
  targets, and owning plan. Validated in CI by
  `build/scripts/validate_capability_registry.sh` (issue #234).

## Provenance

Each document carries a `Last verified against commit: <sha>` line. When you
touch the underlying surface (a syscall signature, a capability ID, the
launcher API, the manifest layout), bump the verification line in the
corresponding doc in the same change.

Last verified against commit: fb6ed647530b1f8ed097ea821ec51ae014854932
