# Syscall ABI

> **Owner:** unassigned
> **Status:** stub
> **Last reviewed:** 2026-05-19
> **Applies to:** `OS_ABI_VERSION = 0`

To be filled by the work tracked in **#93** (ABI reference).

This document will specify, at minimum:

- syscall numbering and stability guarantees
- calling convention per supported architecture (x86 first)
- argument / return register layout
- error model (return-code vs. errno-style) and reserved error space
- capability-handle argument passing rules (cross-reference
  [`capability-handle.md`](./capability-handle.md))
- compatibility / deprecation policy tied to `OS_ABI_VERSION`

Until then, treat the syscall surface as **unstable** and subject to change
without notice.
