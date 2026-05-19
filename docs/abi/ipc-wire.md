# IPC Wire Format and Error Model

> **Owner:** unassigned
> **Status:** stub
> **Last reviewed:** 2026-05-19
> **Applies to:** `OS_ABI_VERSION = 0`

To be filled by the work tracked in **#180** (Plan: M1 synchronous IPC
primitive — BUILD_ROADMAP §5.1).

This document will specify, at minimum:

- synchronous request/response framing
- message header layout (version, opcode, length, capability-handle slots)
- endianness and alignment rules
- error model and reserved error codes (cross-reference the capability-denied
  contract tracked in **#164**)
- versioning rules and how `OS_ABI_VERSION` gates wire-incompatible changes

Until then, the IPC wire format is **undefined** and any in-tree experiments
must be treated as throwaway.
