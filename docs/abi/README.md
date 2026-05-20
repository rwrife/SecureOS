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
- [manifest.md](manifest.md) — Launcher manifest schema: how an app declares
  the capabilities it needs and how the launcher mediates grants today.
- [versioning.md](versioning.md) — `OS_ABI_VERSION` policy, compat-shim
  window, and the process for adding / removing ABI surface.

## Provenance

Each document carries a `Last verified against commit: <sha>` line. When you
touch the underlying surface (a syscall signature, a capability ID, the
launcher API, the manifest layout), bump the verification line in the
corresponding doc in the same change.

Last verified against commit: 9f4f7ccbb19c9ffb28ee4b6de2f3e93c35e65785
