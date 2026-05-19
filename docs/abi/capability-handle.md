# Capability Handle Representation and Revocation

> **Owner:** unassigned
> **Status:** stub
> **Last reviewed:** 2026-05-19
> **Applies to:** `OS_ABI_VERSION = 0`

To be filled by the work tracked in **#163** (Docs: kernel/module/user-lib
boundary conventions) and the broader capability work across M2–M4.

This document will specify, at minimum:

- on-the-wire representation of a capability handle (size, opaqueness,
  process-local vs. global semantics)
- handle table ownership rules (kernel-side capability table vs. user view)
- revocation semantics — when a handle becomes invalid and how callers learn
- the capability-denied error + log marker contract (cross-reference **#164**)
- restrictions on copying / sharing handles across IPC boundaries
  (cross-reference [`ipc-wire.md`](./ipc-wire.md))

Until then, the capability handle representation is **unstable**; do not
persist handle values across boots or rely on numeric layout.
