# M1 → M2 launcher → app initial capability handoff

| Field            | Value                                                 |
| ---------------- | ----------------------------------------------------- |
| Owner            | kernel + launcher                                     |
| Status           | accepted (slice 2 of plan #263 / M2-on-M1 substrate)  |
| Last reviewed    | 2026-05-24                                            |
| Applies to       | `OS_ABI_VERSION = 0`                                  |
| Tracking issue   | #269                                                  |
| Source-of-truth  | `kernel/user/launcher.{c,h}`, `kernel/proc/address_space.{c,h}` |

## Problem

M1 froze three independent surfaces:

* the **process table** + `address_space_t` window (`process_create`, `aspace_partition`, #238 / #248 / #249),
* the **capability handle** layer (`cap_handle_t`, frozen 32-bit layout, #233 / #237 / #240 / #247),
* the **synchronous IPC v0** primitive and its handle-gated peers
  (`ipc_send_h` / `ipc_recv_h`, #220 / #229 / #246).

M2's launcher historically called the gate functions as flat C functions
inside a single host process (`launcher_app_console_write`). It never
crossed a real PCB boundary. To re-platform M2 on top of M1 (plan #263),
the launcher needs to:

1. **Spawn** the app as a real PCB with its own
   `address_space_t` window, and
2. **Hand off** the first `cap_handle_t` to that app **in-band** —
   without inventing a new ABI surface.

This document fixes the in-band handoff convention.

## Convention

Each `address_space_t` carved by `aspace_partition()` reserves the first
`IPC_MSG_PAYLOAD_MAX` (= 64) bytes of its window as the per-process
**IPC scratch region** (`address_space_t::ipc_scratch`,
`kernel/proc/address_space.h`).

For the M1→M2 initial handoff, the launcher writes the freshly minted
`cap_handle_t` into the first **four** scratch bytes, **little-endian**:

```
offset  bytes  meaning
   0      4    cap_handle_t (LE)  — launcher-minted initial grant
   4     60    reserved (zeroed by aspace_partition)
```

The wire is identical to how a future syscall would return the handle
to userspace: a `uint32_t` in target-platform byte order, with the
launcher choosing LE so the convention is portable across the host
test fixtures and the qemu peers.

### Why the scratch region

* It is **per-process** and **bounds-enforced** by the existing
  `aspace_contains()` gate (#260 done-when 1).
* It is **already** the surface the kernel uses to stage `ipc_msg_v0`
  payloads — so the receiving app reads it through the same pointer it
  will use to drain real IPC messages once HelloApp's module body
  (slice 3, #270) lands.
* It avoids inventing a new register-bank ABI or a thread-local-storage
  pointer; both would be ABI surfaces that need their own #150 process.

### Why little-endian

* Matches the on-disk endianness of `cap_handle_t` whenever the cap
  table is persisted (planned for M3 / #82).
* Matches every supported target (host x86_64, qemu x86_64) at
  `OS_ABI_VERSION = 0`. A big-endian port would re-encode in
  `scratch_store_handle()` only.

### Versioning

The slot is **frozen** under `OS_ABI_VERSION = 0`:

* Offsets `[0,4)` are the cap_handle.
* Offsets `[4,64)` are reserved and MUST be zero on handoff.

A future `OS_ABI_VERSION` bump may widen the slot (e.g. to pass a tuple
of `(handle, port)` for handle-port pre-binding); the launcher will be
updated in lockstep with the schema gate (#226) as per the §7 process
in `docs/abi/versioning.md`.

## Lifecycle

```
┌──────────────────────────────────────────────────────────────────┐
│ 1. launcher_spawn_app_from_manifest(manifest, &out_spawn)        │
│      ├── aspace_partition()       → out_spawn.aspace             │
│      ├── process_create()         → out_spawn.pid                │
│      ├── cap_handle_grant(subj,…) → h                            │
│      └── scratch_store_handle(as, h)  ← LE write into [0..4)     │
│                                                                  │
│ 2. App reads first 4 bytes of its ipc_scratch as a cap_handle_t  │
│    and passes it to ipc_send_h() / cap_gate_check_handle().      │
│                                                                  │
│ 3. launcher_spawn_destroy(pid)                                   │
│      └── process_destroy() cascades cap_handle_revoke_subject()  │
│         (per #239) so any stored copy of `h` now fails.          │
└──────────────────────────────────────────────────────────────────┘
```

## Validator

`tests/launcher_spawn_handoff_test.c` (#269 slice 2 acceptance):

* Spawns a no-op PCB from a synthesised `launcher_manifest_t` that
  declares `CAP_CONSOLE_WRITE` as the auto-grant.
* Asserts `out_spawn.aspace->ipc_scratch[0..4)` decodes byte-for-byte
  to the same `cap_handle_t` value returned in
  `out_spawn.granted_handle`.
* Asserts the handle round-trips through
  `cap_gate_check_handle(h, CAP_CONSOLE_WRITE) == 1`.
* Asserts `launcher_spawn_destroy()` revokes the handle
  (`cap_gate_check_handle(h, CAP_CONSOLE_WRITE) == 0`).

The validator is host-only; the qemu peer lands with slice 4 (#271).

## Non-goals

* No new `ipc_result_t` value, no new `capability_id_t`, no
  `OS_ABI_VERSION` bump.
* No HelloApp module body — slice 3 (#270) wires it on top of this
  convention.
* No multi-handle handoff — `auto_grant_caps[0]` is the only slot
  consumed in v0. The remaining 60 bytes of scratch are zeroed and
  reserved.

## Related

* [Plan: `plans/2026-05-23-m2-on-m1-substrate.md`](../../plans/2026-05-23-m2-on-m1-substrate.md)
* [Capability handle layer (`docs/abi/capabilities.md`)](../abi/capabilities.md)
* [Capability deny contract (`docs/abi/capability-deny-contract.md`)](../abi/capability-deny-contract.md)
* [ABI versioning policy (`docs/abi/versioning.md`)](../abi/versioning.md)
