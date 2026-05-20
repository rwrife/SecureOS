# SecureOS IPC Wire Format

> **Owner:** unassigned
> **Status:** stub
> **Last reviewed:** 2026-05-20
> **Applies to:** `OS_ABI_VERSION = 0`

This document is the canonical home for the inter-process communication
(IPC) wire format and error model — one of the four ABI surfaces that
[`BUILD_ROADMAP.md` §7](../../BUILD_ROADMAP.md) requires to be defined and
versioned early.

## Status

**Stub.** The wire format is not yet specified. M1 introduces the
synchronous IPC primitive (BUILD_ROADMAP §5.1); that work, and the
plan tracking it, will populate this document. Until then, do **not**
treat any in-tree IPC helper as a stable contract.

## To be filled by

- **#180** — *Plan: M1 synchronous IPC primitive (BUILD_ROADMAP §5.1)*
  is the parent tracking issue. The plan landing under that issue defines
  the message envelope, endpoint identity, and capability-gated reply
  path that this document must describe.

Once #180's plan is accepted, this stub must grow to cover at minimum:

- Message envelope layout (header fields, alignment, endianness).
- Endpoint addressing and how it maps to capability handles
  (see [`capabilities.md`](./capabilities.md)).
- Synchronous call / reply semantics and timeout behavior.
- Error / status model — must reuse the `OS_STATUS_*` codes already used
  by the syscall surface (see [`syscalls.md`](./syscalls.md)) rather than
  introducing a parallel error space.
- Capability-denied behavior on the IPC path — must align with the
  capability-denied error + log marker contract tracked in **#164**.
- Compatibility policy: what may change inside `OS_ABI_VERSION = 0`
  versus what requires an `OS_ABI_VERSION` bump (per
  [`versioning.md`](./versioning.md)).

## Cross-references

- [`syscalls.md`](./syscalls.md) — user-facing `os_*` syscall surface
  and `OS_STATUS_*` error model the IPC surface must reuse.
- [`capabilities.md`](./capabilities.md) — capability handle
  representation; IPC endpoints are addressed via capability handles.
- [`versioning.md`](./versioning.md) — `OS_ABI_VERSION` policy that
  this surface is bound to.

## Provenance

Stub created to satisfy the §7 "four ABI surfaces" requirement called
out in **#181**. The other three surfaces already have documents in this
directory:

- syscall ABI → [`syscalls.md`](./syscalls.md)
- capability handle representation → [`capabilities.md`](./capabilities.md)
- module manifest schema → [`manifest.md`](./manifest.md)

IPC wire format was the remaining gap. This file fills it as a stub so
the surface inventory is complete; substantive content is owned by #180.

Last verified against commit: de24ea1
