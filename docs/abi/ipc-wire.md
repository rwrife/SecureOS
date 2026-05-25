# SecureOS IPC Wire Format and Error Model

> **Owner:** kernel (M1 IPC primitive)
> **Status:** draft `v0` — normative for `OS_ABI_VERSION = 0`
> **Last reviewed:** 2026-05-20
> **Applies to:** `OS_ABI_VERSION = 0`

This document is the canonical home for the SecureOS inter-process
communication (IPC) wire format and error model — one of the four ABI
surfaces that [`BUILD_ROADMAP.md` §7](../../BUILD_ROADMAP.md) requires to
be defined and versioned early to prevent churn.

It tracks issue **#194** and is the spec counterpart to the M1
synchronous IPC primitive plan
[`plans/2026-05-19-m1-sync-ipc-primitive.md`](../../plans/2026-05-19-m1-sync-ipc-primitive.md)
(closed by #185, parent issue #180). Implementation issues — port table,
`ipc_send` / `ipc_recv` / `ipc_call`, deny-path tests — are enumerated in
that plan; this document fixes the wire shape and error vocabulary they
all must agree on.

## 1. Scope

In scope (v0):

- Message envelope layout (`ipc_msg_v0`): header fields, sizes, byte
  order, alignment, padding.
- Payload framing: maximum payload size, length field interpretation,
  reserved bits.
- Endpoint addressing: how `ipc_port_t` handles relate to the capability
  handle representation in
  [`capabilities.md`](./capabilities.md).
- Operations and their synchronous semantics (`ipc_send`, `ipc_recv`,
  `ipc_call`).
- The IPC error class enum (`ipc_result_t`), including the
  capability-denied path required by the capability-deny contract
  ([#164 / `capability-deny-contract.md`](./capability-deny-contract.md)).
- Versioning rule: how IPC frames declare and are gated against
  `OS_ABI_VERSION` (anchored by [#150](./versioning.md)).

Explicit v0 non-goals (and therefore explicit ABI non-promises):

- No asynchronous, queued, or multicast IPC.
- No zero-copy / shared-memory channels — envelopes are copied on every
  `send` / `recv`.
- No `try_send` / `try_recv` / timeouts. All operations block.
- No cross-address-space delivery across separate processes — v0 runs
  between two in-kernel test modules. The wire format is shaped so a
  later process boundary can adopt it without breaking compatibility.
- No user-space SDK helpers (deferred to M6 / #136).

Adding any of the above is a strict-superset change and may land within
`OS_ABI_VERSION = 0` (additive). Changing what is specified below in a
non-additive way requires bumping `OS_ABI_VERSION` per
[`versioning.md`](./versioning.md).

## 2. Message envelope: `ipc_msg_v0`

```c
struct ipc_msg_v0 {
    uint16_t abi_version;     // MUST equal OS_ABI_VERSION (= 0 in v0)
    uint16_t flags;           // reserved, MBZ in v0
    uint32_t sender_subject;  // cap_subject_id_t of the sender
    uint32_t tag;             // caller-defined message tag (see §2.3)
    uint32_t payload_len;     // 0..IPC_MSG_PAYLOAD_MAX
    uint8_t  payload[IPC_MSG_PAYLOAD_MAX]; // IPC_MSG_PAYLOAD_MAX = 64
};
```

### 2.1 Field semantics

| Offset | Size | Field | Notes |
| ------ | ---- | ----- | ----- |
| 0      | 2    | `abi_version`    | `OS_ABI_VERSION` of the sender. Receiver MUST reject mismatches with `IPC_ERR_INVALID_MSG`. |
| 2      | 2    | `flags`          | Reserved. **Must be zero** (`MBZ`) in v0. Non-zero ⇒ `IPC_ERR_INVALID_MSG`. |
| 4      | 4    | `sender_subject` | Kernel-stamped on send (see §2.4). Senders MAY pre-populate; the IPC layer overwrites with the authenticated `cap_subject_id_t` of the calling subject before delivery. |
| 8      | 4    | `tag`            | Opaque to the IPC layer; reserved bits used by `ipc_call` for reply-port encoding (§2.3). |
| 12     | 4    | `payload_len`    | Number of valid bytes in `payload`. `> IPC_MSG_PAYLOAD_MAX` ⇒ `IPC_ERR_INVALID_MSG`. |
| 16     | 64   | `payload[]`      | Fixed-size copy region. Bytes beyond `payload_len` are unspecified and MUST NOT be read by the receiver. |

Total envelope size in v0: **80 bytes**. The size is fixed by the ABI;
changing `IPC_MSG_PAYLOAD_MAX`, the header layout, or the trailing
padding rule is **not** an additive change and requires an
`OS_ABI_VERSION` bump.

### 2.2 Byte order, alignment, padding

- All multi-byte fields are **little-endian**.
- All fields are **naturally aligned** at their offset; there is **no
  implicit padding** anywhere in the struct.
- The envelope is a value type — implementations MAY pass it by
  reference but MUST treat it as a single contiguous 80-byte
  copy-on-rendezvous unit.
- `payload` MUST NOT be reinterpreted across the receive boundary
  without the receiver also bounds-checking `payload_len`. Bytes in
  `payload[payload_len .. IPC_MSG_PAYLOAD_MAX)` are unspecified.

### 2.3 `tag` field and reply-port encoding (used by `ipc_call`)

`tag` is opaque to `ipc_send` / `ipc_recv`. For `ipc_call` the layout
inside `tag` is reserved for the reply-port handle plus a caller cookie;
the exact bit allocation is fixed by the execute issue that lands
`ipc_call` (per
[`plans/2026-05-19-m1-sync-ipc-primitive.md`](../../plans/2026-05-19-m1-sync-ipc-primitive.md))
and will be documented here in the same PR. Until then, `tag` is treated
as caller-opaque in v0 — receivers MUST NOT decode it.

### 2.4 Sender authentication

`sender_subject` is **kernel-stamped**, not trusted from user input:

- On `ipc_send` / `ipc_call`, the IPC layer overwrites
  `msg->sender_subject` with the calling subject's `cap_subject_id_t`
  immediately after the capability check and before the rendezvous copy.
- Receivers MAY rely on `sender_subject` for policy decisions.
- A receiver that observes a delivered envelope with
  `sender_subject == 0` MUST treat it as `IPC_ERR_INVALID_MSG` — a
  zero subject id is reserved as "unset".

## 3. Endpoint addressing

IPC endpoints are addressed by **opaque port handles**, not by name:

- `ipc_port_t` is a 32-bit handle allocated from a kernel-owned port
  table (`kernel/ipc/ipc_port.{c,h}` per the M1 plan). The v0
  implementation packs the handle as `(generation << 16) | index`,
  where `index` is the low 16 bits (table slot, `0..IPC_PORT_TABLE_MAX-1`)
  and `generation` is a 16-bit counter that is bumped every time a slot
  is destroyed and skipped over `0` on wrap-around, so a freshly
  destroyed handle is never equal to `IPC_PORT_INVALID` (= `0`) and
  never aliases the next handle issued for the same slot. The lifecycle
  contract is locked in by `tests/ipc_port_lifecycle_test.c` (issue
  #223): create → destroy → create returns a strictly different handle
  even when the underlying slot is reused, every read accessor rejects
  the stale handle with `IPC_ERR_INVALID_PORT`, and 65535 create/destroy
  cycles on the same slot index never produce `IPC_PORT_INVALID`.
- The exact bit layout (generation counter vs. table index) is owned by
  [`capabilities.md`](./capabilities.md) and shared with other
  capability-handle types so that revocation semantics are uniform.
- Each port carries an owning subject and the two capability ids
  required to interact with it:
  - `recv_cap` — required to receive on the port (default
    `CAP_IPC_RECV`).
  - `send_cap` — required to send to the port (default
    `CAP_IPC_SEND`; ports MAY declare a more specific cap, e.g. a
    future `CAP_CONSOLE_IPC`).

A port handle is **not** a file descriptor and does not appear in the
syscall ABI as one. It is a capability handle that the syscall layer
([`syscalls.md`](./syscalls.md)) carries as an opaque `uint32_t`.

## 4. Operations

Three primitives in v0, all synchronous, all deny-by-default:

| Op | Signature (sketch) | Required cap | Semantics |
| -- | ------------------ | ------------ | --------- |
| `ipc_send` | `(ipc_port_t target, const ipc_msg_v0 *msg) → ipc_result_t` | sender holds `target.send_cap` | Blocks until a receiver dequeues the envelope. |
| `ipc_recv` | `(ipc_port_t self, ipc_msg_v0 *out) → ipc_result_t`         | caller owns `self` and holds `self.recv_cap` | Blocks until a sender rendezvouses on `self`. |
| `ipc_call` | `(ipc_port_t target, const ipc_msg_v0 *req, ipc_msg_v0 *reply) → ipc_result_t` | sender holds `target.send_cap` and owns the reply port | `send` + `recv` round trip on a caller-owned reply port. Reply-port handle is embedded in `req.tag` (§2.3). |

All three ops route through `cap_gate`-style helpers
(`cap_ipc_send_gate`, `cap_ipc_recv_gate`) so the capability check
emits `CAP_AUDIT_OP_CHECK` events into the existing
`cap_audit_event_t` ring with **no new audit op codes**.

There is no `try_send` / `try_recv` / `timeout` variant in v0. Their
slot in the error enum (`IPC_ERR_WOULD_BLOCK`, §5) is reserved.

## 5. Error model (`ipc_result_t`)

The IPC layer surfaces a small, frozen-for-v0 error enum. It is the
**only** vocabulary returned by `ipc_send` / `ipc_recv` / `ipc_call`.

| Value | Name | Meaning |
| ----- | ---- | ------- |
| 0 | `IPC_OK` | Operation completed; for `ipc_recv` / `ipc_call`, the out-envelope is populated. |
| 1 | `IPC_ERR_INVALID_PORT` | Port handle is not in the table, has been destroyed, or generation counter mismatched. (Transport fault — not a capability decision.) |
| 2 | `IPC_ERR_INVALID_MSG` | `abi_version` mismatch, `payload_len > IPC_MSG_PAYLOAD_MAX`, reserved `flags` set, or `sender_subject == 0` on delivery. (Malformed message.) |
| 3 | `IPC_ERR_CAP_DENIED` | Caller lacks the required capability for the requested op. Path **must** emit the capability-denied marker per [`capability-deny-contract.md`](./capability-deny-contract.md) (e.g. `CAP:DENY:<subject>:CAP_IPC_SEND`) **before** returning. This is the only IPC error that maps to a capability decision and the only one tests should treat as such. |
| 4 | `IPC_ERR_WOULD_BLOCK` | Reserved for a future non-blocking path. V0 implementations MUST NOT return this — they block instead and ultimately return `IPC_OK`. Consumers MAY test for it for forward compatibility. |
| 5 | `IPC_ERR_PEER_GONE` | Peer port was destroyed mid-rendezvous (`ipc_call` only). |
| 6 | `IPC_ERR_BOUNDS` | Caller-supplied envelope buffer escapes the caller's `address_space_t` window (the M1 flat-with-bounds substitute for page-table enforcement — see [`address_space.h`](../../kernel/proc/address_space.h) / `aspace_contains`). Path **must** emit `CAP:DENY:<subject>:ipc_send:bounds` (or `:ipc_recv:bounds`) via the canonical formatter before returning. Returned by `ipc_send` / `ipc_recv` (and their `_h` handle-gated variants) when the call originates from a subject with a live PCB whose aspace does not contain the envelope (issue #260). Subjects with no live PCB skip the check for backward compatibility with the v0 in-kernel test harnesses. |

### 5.1 Error class taxonomy

`ipc_result_t` is partitioned into three classes (cross-references #194's
"capability-denied vs. transport-fault vs. malformed-message" requirement
and #164's deny-marker contract):

- **Capability decision:** `IPC_ERR_CAP_DENIED`. Only this value indicates
  that the kernel's capability check rejected the op. Deny-path tests
  MUST assert on this value **and** on the standard capability-denied log
  marker emitted per [`capability-deny-contract.md`](./capability-deny-contract.md).
- **Malformed message:** `IPC_ERR_INVALID_MSG`. Indicates the envelope
  itself is not parseable under `OS_ABI_VERSION`. MUST NOT be silently
  collapsed into `IPC_ERR_CAP_DENIED`.
- **Bounds violation:** `IPC_ERR_BOUNDS`. The envelope was well-formed,
  the caller was authorized, but the buffer's byte range escapes the
  caller's `address_space_t` window. The deny marker uses the
  resource tag `bounds` so consumers can grep it independently of the
  permissions deny (resource `-`). See `aspace_contains` for the
  exact containment predicate.
- **Transport fault:** `IPC_ERR_INVALID_PORT`, `IPC_ERR_PEER_GONE`. The
  envelope was well-formed and the caller was authorized, but the
  rendezvous could not complete.

### 5.2 Mapping to `os_status_t`

Where an IPC error must surface through the syscall ABI's
`os_status_t` ([`syscalls.md`](./syscalls.md)) — e.g. through a future
`os_ipc_*` syscall — the mapping is:

| `ipc_result_t` | `os_status_t` |
| --- | --- |
| `IPC_OK` | `OS_STATUS_OK` |
| `IPC_ERR_CAP_DENIED` | `OS_STATUS_DENIED` |
| `IPC_ERR_INVALID_PORT`, `IPC_ERR_PEER_GONE` | `OS_STATUS_ERROR` |
| `IPC_ERR_INVALID_MSG`, `IPC_ERR_WOULD_BLOCK` | `OS_STATUS_ERROR` |
| `IPC_ERR_BOUNDS` | `OS_STATUS_ERROR` |

This preserves the
[`syscalls.md`](./syscalls.md) invariant that `OS_STATUS_DENIED` is
the **only** denied result a caller ever observes for a missing
capability and is never collapsed into `OS_STATUS_ERROR`.

## 6. Versioning

- Every envelope carries `abi_version`. Receivers MUST reject envelopes
  whose `abi_version != OS_ABI_VERSION` with `IPC_ERR_INVALID_MSG`.
- The single source of truth for `OS_ABI_VERSION` is the constant
  anchored by [#150](./versioning.md). The IPC layer MUST consume that
  constant rather than redefining its own.
- Additive changes within `OS_ABI_VERSION = 0`:
  - Adding new entries to `ipc_result_t` is **not** additive — the enum
    is frozen for v0. Adding new error classes requires an
    `OS_ABI_VERSION` bump.
  - Defining new bits in `flags` (today MBZ) requires a bump because
    existing receivers reject non-zero flags.
  - Defining new structural meaning for bits inside `tag` is allowed
    additively, because the IPC layer treats `tag` as opaque.
  - Adding new operations (e.g. `ipc_try_send`) is additive, provided
    they use a separate entry point and do not change the existing
    envelope or error vocabulary.
- Non-additive changes (header layout, `IPC_MSG_PAYLOAD_MAX`, byte
  order, alignment, enum semantics) follow the bump policy in
  [`versioning.md`](./versioning.md).

## 7. Cross-references

- [`syscalls.md`](./syscalls.md) — `OS_STATUS_*` model that IPC errors
  map onto when surfaced through syscalls. The IPC layer MUST reuse
  `os_status_t` rather than introducing a parallel surface code at the
  syscall boundary.
- [`capabilities.md`](./capabilities.md) — capability handle
  representation; `ipc_port_t` shares this representation and
  revocation lifecycle.
- [`capability-deny-contract.md`](./capability-deny-contract.md) —
  authoritative shape of the capability-denied log marker that
  `IPC_ERR_CAP_DENIED` MUST emit before returning (per #164).
- [`versioning.md`](./versioning.md) — `OS_ABI_VERSION` policy that
  this surface is bound to (anchored by #150).
- [`manifest.md`](./manifest.md) — how a module declares the IPC ports
  it owns and the `send_cap` / `recv_cap` it requires (ties into the
  manifest schema landed in #187).
- [`plans/2026-05-19-m1-sync-ipc-primitive.md`](../../plans/2026-05-19-m1-sync-ipc-primitive.md)
  — the M1 sync-IPC primitive plan that this document specifies the
  wire format for (#180 / #185).
- `BUILD_ROADMAP.md` §5.1 — M1 minimal kernel isolation + IPC
  skeleton; §7 — ABI freeze plan.

## 8. Provenance

Stub created under #181 (docs/abi scaffold) and filled by **#194** to
specify the IPC wire format + error model before the M1 sync-IPC
implementation work begins under #180 / #185. Implementation issues that
must conform to this surface are enumerated in the M1 plan referenced
above.

Last verified against commit: 7f303d7e901d6707e6f223a2b1fa1b0621792963
