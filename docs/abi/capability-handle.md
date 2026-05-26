# SecureOS Capability Handle ABI

> **Owner:** kernel / capability subsystem (handle layer)
> **Status:** normative `v0` — frozen at `OS_ABI_VERSION = 0`
> **Last reviewed:** 2026-05-26
> **Applies to:** `OS_ABI_VERSION = 0`
> **Tracking issue:** [#233](https://github.com/rwrife/SecureOS/issues/233)
> (parent plan: [#197](https://github.com/rwrife/SecureOS/issues/197))
> **Implementation:** `kernel/cap/cap_handle.{h,c}`

This document is the canonical ABI home for the **capability handle**
surface called out by [`BUILD_ROADMAP.md` §7](../../BUILD_ROADMAP.md) —
"capability handle representation + revocation". It is intentionally
separated from the broader [`capabilities.md`](capabilities.md) (which
covers the `capability_id_t` enum, gate semantics, audit, and the
launcher-mediated grant model) so the bit-layout and gate-check
contracts here can be reasoned about, and frozen, independently.

The text in [`capabilities.md` §"32-bit packed handle"](capabilities.md)
is a summary; this file is the normative reference. If the two ever
disagree, **this file wins** and the summary must be corrected.

## 1. Type and constants

```c
typedef uint32_t cap_handle_t;

#define CAP_HANDLE_NULL          ((cap_handle_t)0u)

#define CAP_HANDLE_SLOT_BITS     16u
#define CAP_HANDLE_GEN_BITS      14u
#define CAP_HANDLE_TAG_BITS      2u

#define CAP_HANDLE_SLOT_SHIFT    0u
#define CAP_HANDLE_GEN_SHIFT     16u   /* SLOT_BITS */
#define CAP_HANDLE_TAG_SHIFT     30u   /* SLOT_BITS + GEN_BITS */

#define CAP_HANDLE_TAG_KERNEL    0x1u  /* 0b01 */

#define CAP_HANDLE_TABLE_MAX     64u   /* v0 row count */
```

The three field widths MUST sum to 32. A compile-time `#error` in
`kernel/cap/cap_handle.h` enforces both that invariant and
`OS_ABI_VERSION == 0`. Any change to any of these constants is a
v0 → v1 ABI break and MUST follow [`versioning.md`](versioning.md) §"ABI
bump checklist".

## 2. Bit layout

The 32-bit handle is little-endian-packed in the natural integer sense:

```
 31 30 29              16 15                    0
+-----+-------------------+----------------------+
| tag | generation (low14)|        slot (16)     |
+-----+-------------------+----------------------+
```

| Bits   | Width | Field        | Meaning                                                                                                                              |
| ------ | ----- | ------------ | ------------------------------------------------------------------------------------------------------------------------------------ |
| 0..15  | 16    | `slot`       | Index into the global `cap_handle_row` table (`0 .. CAP_HANDLE_TABLE_MAX-1`, currently 64).                                          |
| 16..29 | 14    | `generation` | Low 14 bits of the row's monotonic `uint32_t` generation counter. The row keeps the full 32 bits; only the low 14 are reified on the wire. Collisions are not possible until 16 384 revocations of the same slot, which exceeds the M1 bound. |
| 30..31 | 2     | `tag`        | `0b00` = null/invalid (`CAP_HANDLE_NULL` lives here), **`0b01` = kernel capability handle** (the only value used today), `0b10` / `0b11` reserved for future handle kinds (file handles, IPC ports, broker tickets). |

`CAP_HANDLE_NULL` is the all-zero handle (`tag = 0b00`, `slot = 0`,
`generation = 0`) and always fails the gate with `CAP_ERR_CAP_INVALID`.

### Pack / unpack helpers

```c
cap_handle_t cap_handle_pack(uint16_t slot,
                             uint16_t generation_low14,
                             uint8_t  tag);
uint16_t     cap_handle_slot(cap_handle_t handle);
uint16_t     cap_handle_generation(cap_handle_t handle);
uint8_t      cap_handle_tag(cap_handle_t handle);
```

These helpers are part of the ABI for tests and trampoline code; they
perform no validation. The gate-check entry points are the only path
that consults the live row.

## 3. Row representation (kernel-internal)

The handle is the externally observable identifier. The row it points to
is kernel-internal but documented here because the gate-check contract
references it:

```c
struct cap_handle_row {
  uint16_t          abi_version;     /* always OS_ABI_VERSION = 0 in v0 */
  uint16_t          flags;           /* CAP_HANDLE_FLAG_LIVE | _REVOKED | _SEALED */
  capability_id_t   cap_id;
  cap_subject_id_t  owner_subject;
  cap_subject_id_t  granter_subject;
  uint32_t          parent_handle;   /* reserved, MBZ in v0 (see §6) */
  uint32_t          generation;
};
```

`flags` bit 3+ MBZ in v0. `parent_handle` MBZ in v0 (see §6, "Subtree
revoke").

The table is statically allocated at `CAP_HANDLE_TABLE_MAX = 64` rows
(no heap dependency, per `docs/CODING_CONVENTIONS.md` / [#163]). The
table is global, not per-process, because subject identity is the
authorization principal at M1 and below.

## 4. Gate-check contract

```c
int            cap_gate_check_handle       (cap_handle_t handle,
                                            capability_id_t expected_cap);
cap_result_t   cap_gate_check_handle_result(cap_handle_t handle,
                                            capability_id_t expected_cap);
```

Both entry points perform the **identical** sequence of checks, in the
following normative order. Both are pure (no audit emission, no table
mutation, no allocation). The boolean form returns `true` iff the
detailed form would return `CAP_OK`.

| Order | Check                                                              | Failure code           |
| ----- | ------------------------------------------------------------------ | ---------------------- |
| 1     | `cap_handle_tag(handle) == CAP_HANDLE_TAG_KERNEL` (i.e. `0b01`)    | `CAP_ERR_CAP_INVALID`  |
| 2     | `cap_handle_slot(handle) < CAP_HANDLE_TABLE_MAX`                   | `CAP_ERR_CAP_INVALID`  |
| 3     | Target row has `CAP_HANDLE_FLAG_LIVE` set                          | `CAP_ERR_MISSING`      |
| 4     | `cap_handle_generation(handle) == (row.generation & 0x3FFF)`       | `CAP_ERR_MISSING`      |
| 5     | `row.cap_id == expected_cap`                                       | `CAP_ERR_CAP_INVALID`  |

Rationale for the **stale vs. malformed** split:

- `CAP_ERR_MISSING` ("the grant is gone") is reported when the handle
  refers to a row that was once-live but isn't now — either because the
  row has been revoked or because the slot was reissued at a newer
  generation. This is what callers must distinguish from
  never-granted, so audit / launcher prompts can differentiate.
- `CAP_ERR_CAP_INVALID` ("the handle could never have been valid") is
  reported when the bits themselves are wrong (bad tag, OOB slot, or
  the row authorizes a *different* capability than `expected_cap`).

The syscall trampoline ([#192]) translates the boolean form: `true →
OS_STATUS_OK`, `false → OS_STATUS_DENIED`. Callers that need the
specific cap_result_t (audit, broker, tests) use the `_result` form.

`CAP_HANDLE_NULL` always fails at check #1 with `CAP_ERR_CAP_INVALID`.

## 5. Mint / revoke contract

```c
cap_handle_t  cap_handle_grant         (cap_subject_id_t owner_subject,
                                        capability_id_t  cap_id);
cap_result_t  cap_handle_revoke        (cap_handle_t     handle);
uint32_t      cap_handle_revoke_subject(cap_subject_id_t owner_subject);
cap_subject_id_t cap_handle_owner      (cap_handle_t     handle);
```

### `cap_handle_grant(owner, cap)`

- Allocates a row for `(owner, cap)` if none is live and returns a
  handle whose `tag = 0b01`, `slot = <row index>`, and
  `generation = row.generation & 0x3FFF`.
- **Idempotent.** A second grant for an *already-live* `(owner, cap)`
  returns the **same handle** (same slot, same generation). The row is
  not duplicated and the generation is not bumped.
- Returns `CAP_HANDLE_NULL` on any error (bad subject id, bad cap id,
  or table full). `CAP_HANDLE_NULL` always fails `cap_gate_check_handle`.

### `cap_handle_revoke(handle)`

- Checks `handle` against the live row in the same way as
  `cap_gate_check_handle_result` (steps 1–4 only — the `cap_id` step is
  unnecessary because the row's cap_id is authoritative).
- On match: clears `CAP_HANDLE_FLAG_LIVE`, sets `CAP_HANDLE_FLAG_REVOKED`,
  bumps `row.generation` by 1 (32-bit, wraps after 2³² revokes per
  slot), and returns `CAP_OK`. The row is **not** freed in v0; the
  slot is permanently retired (a future v1 may add slot reuse with a
  generation guard, but v0 callers MUST NOT depend on reuse).
- After a successful revoke the same numeric handle value fails the
  gate at check #4 (`CAP_ERR_MISSING`), permanently. Any handle
  previously issued for that row stales in the same step.
- Returns `CAP_ERR_MISSING` if the row is already revoked or if the
  generation is stale; `CAP_ERR_CAP_INVALID` if the handle is
  malformed.

### `cap_handle_revoke_subject(owner)` (M1-CAPTBL-003, [#239])

- Single linear pass over the global table. For each row whose
  `owner_subject == owner` and `flags & CAP_HANDLE_FLAG_LIVE`:
  transition to REVOKED and bump generation by 1.
- Returns the number of rows revoked (0 if `owner` is out of range or
  held no live rows).
- **Best-effort by contract:** there is no error code. Callers that
  need a per-handle result use `cap_handle_revoke` instead.
- Called by `kernel/proc/process.c::process_destroy` so process exit
  authoritatively stales every capability handle the process held.

### `cap_handle_owner(handle)`

- Returns `row.owner_subject` for a handle that would pass checks #1–#4
  of the gate (live row, generation match). The `expected_cap` step is
  intentionally skipped — handle-gated IPC paths ([M1-CAPTBL-006],
  [#246]) need the authenticated subject *before* they know which cap
  to gate against.
- Returns `0` (the reserved invalid subject id) on any handle that
  would fail the gate. No audit emission, no table mutation.

## 6. Subtree revoke (reserved at v0)

```c
cap_result_t cap_handle_revoke_subtree(cap_handle_t root_handle);
```

This symbol is **declared and frozen at v0** so downstream work — the
M4 broker subtree-revoke ([#115]) and the M5 ownership-graph cascading
deletion ([#118]) — has a stable kernel entry point to compile against.

**v0 contract:** unconditionally returns `CAP_ERR_CAP_INVALID`. No
table mutation, no audit emission, no other side effects. The
`parent_handle` field on `cap_handle_row` is reserved-and-zero in v0,
so there is no graph to walk yet.

Callers MUST NOT depend on subtree-revoke semantics until [#118] /
[#115] land the real implementation. That work is allowed to change
the error vocabulary; this stub only reserves the symbol shape.
Tracking: M1-CAPTBL-004 / [#241], and the M5-SUBSTRATE-001 slice
[#323] / [#118].

## 7. Audit interaction

The handle layer itself emits **no** audit events. Audit is anchored
on the legacy `cap_check` / `cap_grant` / `cap_revoke` path, which is
also where the byte-exact fixture-diff test
(`tests/capability_audit_fixture_test.c`,
[`capabilities.md` §"Byte-exact audit fixture-diff"](capabilities.md))
is pinned.

When the façade migration ([M1-CAPTBL-005]) routes legacy `cap_table_*`
through the handle layer, the audit-ring byte stream MUST stay
identical or that test fails loudly with a per-line diff. Updating the
fixture is allowed only under an explicit audit-ABI bump per
BUILD_ROADMAP §7.

## 8. Compile-time freeze

`kernel/cap/cap_handle.h` enforces the v0 freeze with two static
guards:

```c
#if (CAP_HANDLE_SLOT_BITS + CAP_HANDLE_GEN_BITS + CAP_HANDLE_TAG_BITS) != 32u
#  error "cap_handle_t layout must consume exactly 32 bits at OS_ABI_VERSION=0"
#endif
#if OS_ABI_VERSION != 0
#  error "cap_handle_t layout is frozen at OS_ABI_VERSION=0; bump requires v1 audit"
#endif
```

`OS_ABI_VERSION` is sourced from
[`user/include/secureos_abi.h`](../../user/include/secureos_abi.h) —
the same single anchor used by every other ABI-frozen header
([#150] / [#228]). The validator
`build/scripts/validate_abi_stamps.sh` walks `docs/abi/*.md` against
`git log` to make sure the "Last verified against commit" line at the
end of this file is bumped whenever the underlying
`kernel/cap/cap_handle.{h,c}` source actually changes.

## 9. Test surface

The handle ABI is exercised by, at minimum, these targets (all green
on `main` as of the verification commit below):

- `tests/cap_handle_repr_test.c` — pack/unpack round-trips, layout
  constants, ABI-freeze static asserts mirrored at runtime, idempotent
  grant returns same handle, revoke → stale-generation deny shape.
- `tests/cap_handle_revoke_subject_test.c` — bulk-revoke pass over
  the global table; count and per-handle staleness assertions.
- `tests/cap_handle_revoke_subtree_test.c` — v0 stub returns
  `CAP_ERR_CAP_INVALID` for any input including a live handle.
- `tests/capability_audit_fixture_test.c` — byte-exact audit ring
  fixture (gates the M1-CAPTBL-005 façade migration).
- `tests/ipc_handle_gate_test.c` — `cap_handle_owner` + gate-check
  used by the M1 IPC primitive.

Run via `./build/scripts/test.sh cap_handle_repr` (or the matching
`_revoke_subject` / `_revoke_subtree` targets); host-side, the
host-side wrapper `./scripts/test.sh cap_handle_repr` will run the
same test inside the pinned toolchain container.

## 10. To-be-filled / open follow-ups

The handle representation itself is **done** at v0. Adjacent slices
that continue to evolve in their own files / tracking issues:

- M1-CAPTBL-004 — real `cap_handle_revoke_subtree` walker.
  Tracked under [#241] (kernel symbol stub today) and
  [#323] (M5-SUBSTRATE-001, the BFS walker + `cap_handle_grant_child`
  parent-handle plumbing).
- M1-CAPTBL-005 — façade migration of `cap_table_*` onto the handle
  layer, gated on the byte-exact audit fixture.
- M1-CAPTBL-006 ([#246]) — IPC integration: handle-gated `ipc_send_h`
  / `ipc_recv_h` consume the handles defined here.

These are tracked in their own issues and have their own ABI text
(`ipc-wire.md`, `capabilities.md`) where appropriate. This file owns
the bit layout, the gate-check contract, and the mint/revoke API, all
of which are frozen at v0.

## Provenance

Last verified against commit: 1d92bf5328fa8d804bcbddc19d54e3ad9a8e6fc3

[#115]: https://github.com/rwrife/SecureOS/issues/115
[#118]: https://github.com/rwrife/SecureOS/issues/118
[#150]: https://github.com/rwrife/SecureOS/issues/150
[#163]: https://github.com/rwrife/SecureOS/issues/163
[#192]: https://github.com/rwrife/SecureOS/issues/192
[#228]: https://github.com/rwrife/SecureOS/issues/228
[#239]: https://github.com/rwrife/SecureOS/issues/239
[#241]: https://github.com/rwrife/SecureOS/issues/241
[#246]: https://github.com/rwrife/SecureOS/issues/246
[#323]: https://github.com/rwrife/SecureOS/issues/323
[M1-CAPTBL-005]: https://github.com/rwrife/SecureOS/issues/243
[M1-CAPTBL-006]: https://github.com/rwrife/SecureOS/issues/246
