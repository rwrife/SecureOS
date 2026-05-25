# SecureOS Capability Handle ABI

> **Owner:** kernel / capability subsystem (handle layer)
> **Status:** draft `v0` — stub; normative layout pending M1-CAPTBL-002
> **Last reviewed:** 2026-05-25
> **Applies to:** `OS_ABI_VERSION = 0`
> **Tracking issue:** [#233](https://github.com/rwrife/SecureOS/issues/233)
> (parent plan: [#197](https://github.com/rwrife/SecureOS/issues/197))

This document is the canonical ABI home for the **capability handle**
surface called out by [`BUILD_ROADMAP.md` §7](../../BUILD_ROADMAP.md) —
"capability handle representation + revocation". It is intentionally
separated from the broader [`capabilities.md`](capabilities.md) (which
covers the `capability_id_t` enum, gate semantics, audit, and the
launcher-mediated grant model) so that the handle bit-layout and
`cap_gate_check_handle()` contract can be frozen independently as part of
M1-CAPTBL-002.

## Scope

This file will, once #233 lands, normatively specify:

- The **32-bit handle layout** (`cap_handle_t`): generation / index /
  reserved-bit split, invalid-handle sentinel, and packing rules.
- The **revocation contract**: how a freed slot bumps its generation so
  that stale handles cap-deny with a stable `CAP:DENY:` marker rather
  than aliasing a new grant.
- The **`cap_gate_check_handle()` signature and error model**: inputs,
  outputs, and which `ipc_result_t` / capability error codes it
  surfaces (cross-reference [`ipc-wire.md`](ipc-wire.md) §error model).
- The **ABI freeze status**: handles are intended to be ABI-frozen
  before `OS_ABI_VERSION` advances past `0`; until then, layout changes
  must update this file and bump
  [`versioning.md`](versioning.md) accordingly.

## To be filled by

[#233 — M1 capability handle layer (M1-CAPTBL-002): 32-bit handle +
`cap_gate_check_handle`, ABI-frozen (from plan #197)](https://github.com/rwrife/SecureOS/issues/233).

Until that issue lands, treat
[`capabilities.md`](capabilities.md) and
[`docs/architecture/CAPABILITIES.md`](../architecture/CAPABILITIES.md)
as the de-facto source of truth for capability semantics; this file is a
placeholder so that #233's ABI text has an unambiguous landing slot and
so that the four §7 surfaces (syscalls, IPC wire, capability handle,
manifest) each have a dedicated page per #181.

## Provenance

Last verified against commit: f73b31b6a8cf992923e3b6d82e67644bcf9059ee
