# 2026-05-19 M1 Synchronous IPC Primitive (Plan)

**Status:** Implemented in PR for #210 (v0 scaffold: `kernel/ipc/` with `ipc_port.{c,h}`, `ipc_msg.h`, `ipc_ops.{c,h}`, capability gates `CAP_IPC_SEND` / `CAP_IPC_RECV`, deterministic test target `ipc_sync_v0`). Plan-level non-goals remain deferred to follow-up issues.
**Tracks:** BUILD_ROADMAP §5.1 (M1 minimal kernel isolation + IPC skeleton).
**Owner:** kernel
**Last reviewed:** 2026-05-19
**Related:** #92 (M2 deny path), #108 (M3 deny path), #115 (M4 broker), #93 / #150 / #181 (ABI), #164 (capability-denied marker), #163 (kernel/module/user-lib boundaries), #149 (plan directory drift).

## Motivation

BUILD_ROADMAP §5.1 requires a synchronous IPC primitive so two modules can exchange a message and an unauthorized operation is denied with an explicit error. The repo currently has **no `kernel/ipc/` directory**. Every later milestone (M2 console service, M3 fs service, M4 broker) stacks on this primitive, so we need a shared, capability-gated, deterministic design *before* code lands.

This plan is intentionally narrow: define the smallest vertical slice that satisfies §5.1's two validation bullets ("two modules exchange message" and "unauthorized operation denied with explicit error") and registers a stable ABI surface under `docs/abi/`.

## Non-goals

- Asynchronous / queued / multicast IPC (defer to a later milestone).
- Shared memory / zero-copy transports (only fixed-size envelope copy in v0).
- User-space wrappers / SDK helpers (defer to M6 / #136).
- Cross-address-space IPC across separate processes — v0 lives inside the kernel between two in-kernel test modules; the ABI is shaped so a later process boundary can adopt it without breaking the wire format.
- Threading primitives beyond a single blocked waiter per endpoint.

## Design surface

### Endpoint / port handle type

- New opaque handle: `ipc_port_t` (32-bit; layout TBD by execute issue), allocated from a kernel-owned port table in `kernel/ipc/ipc_port.{c,h}`.
- Each port carries:
  - `owner_subject_id` (cap_subject_id_t) — the module that registered/listens on the port.
  - `capability_id_t recv_cap` — capability required to *receive* on the port (default `CAP_IPC_RECV`).
  - `capability_id_t send_cap` — capability required to *send* to the port (default `CAP_IPC_SEND`; ports may declare a more specific cap, e.g. a future `CAP_CONSOLE_IPC`).
  - single-waiter slot (v0: one blocked sender + one blocked receiver max).
- Port handles are *not* file descriptors; they share the capability-handle representation surface tracked by #150 / `docs/abi/capability-handle.md`.

### Message envelope layout

Fixed, versioned, ABI-frozen via `OS_ABI_VERSION` (#150):

```
struct ipc_msg_v0 {
  uint16_t abi_version;     // == OS_ABI_VERSION
  uint16_t flags;           // reserved, MBZ in v0
  uint32_t sender_subject;  // cap_subject_id_t of sender
  uint32_t tag;             // caller-defined message tag
  uint32_t payload_len;     // bytes valid in payload[]; <= IPC_MSG_PAYLOAD_MAX
  uint8_t  payload[IPC_MSG_PAYLOAD_MAX]; // IPC_MSG_PAYLOAD_MAX = 64 in v0
};
```

- All fields little-endian, naturally aligned, no implicit padding.
- `IPC_MSG_PAYLOAD_MAX` is a compile-time constant exported through the syscall ABI header (#93 / #150).
- v0 deliberately copies the full envelope on send/recv to keep the wire format trivially auditable.

### Operations (synchronous, blocking)

Three primitives, deny-by-default:

| op | signature (sketch) | semantics |
| -- | ------------------ | --------- |
| `ipc_send` | `(ipc_port_t target, const ipc_msg_v0 *msg) -> ipc_result_t` | Blocks until a receiver dequeues the envelope. Requires sender holds the port's `send_cap`. |
| `ipc_recv` | `(ipc_port_t self, ipc_msg_v0 *out) -> ipc_result_t` | Blocks until an envelope is delivered to `self`. Requires caller holds the port's `recv_cap` and owns the port. |
| `ipc_call` | `(ipc_port_t target, const ipc_msg_v0 *req, ipc_msg_v0 *reply) -> ipc_result_t` | `send` + `recv` round-trip on a caller-owned reply port; reply port handle is embedded in `req.tag` reserved bits (exact encoding owned by execute issue). |

v0 does *not* expose `try_send` / `try_recv` / timeouts; those are explicitly deferred.

### Error model

New `ipc_result_t` enum, with the capability-denied path reusing the marker contract from #164:

- `IPC_OK = 0`
- `IPC_ERR_INVALID_PORT` — handle not in port table.
- `IPC_ERR_INVALID_MSG` — `abi_version` mismatch, `payload_len > IPC_MSG_PAYLOAD_MAX`, or reserved bits set.
- `IPC_ERR_CAP_DENIED` — caller lacks the required capability. Path **must** emit the standard capability-denied log marker (`CAP:DENY:<subject>:<cap>` per #164) before returning so deny-path tests can grep deterministically.
- `IPC_ERR_WOULD_BLOCK` — reserved for a future non-blocking path; v0 implementations return `IPC_OK` after blocking.
- `IPC_ERR_PEER_GONE` — peer port destroyed mid-call (relevant for `ipc_call`).

### Capability table interaction

- Extend `capability_id_t` with `CAP_IPC_SEND` and `CAP_IPC_RECV` (next free numeric IDs; constants owned by execute issue, locked under `OS_ABI_VERSION`).
- All three ops route through `kernel/cap/cap_gate.c` style helpers (`cap_ipc_send_gate`, `cap_ipc_recv_gate`) so the existing audit pipeline (`cap_audit_event_t`) records `CAP_AUDIT_OP_CHECK` events identically to console/serial/fs paths. No new audit op codes.
- Grant flow for tests goes through `cap_table` exactly like the console slice (#92 precedent); no implicit grants.

### ABI surface to register

Add entries (stubs land first, content follows in execute issues) under:

- `docs/abi/syscall.md` — three IPC syscalls, their numbers (allocated when locked by #150 / #93).
- `docs/abi/ipc-wire.md` — `ipc_msg_v0` layout, endianness, padding rules, `IPC_MSG_PAYLOAD_MAX`.
- `docs/abi/capability-handle.md` — `ipc_port_t` encoding + lifecycle (alloc, destroy, revoke interaction).
- `docs/abi/manifest.md` — how a module declares which IPC ports it owns and which `send_cap`/`recv_cap` it requires (ties into #183).

Each entry must reference `OS_ABI_VERSION` and bump it on any incompatible change.

## Smallest verifiable vertical slice

Two in-kernel test modules under `kernel/tests/ipc/`:

1. `ipc_ping_module` — owns port `P_PING`, registers `CAP_IPC_RECV` on itself, calls `ipc_recv`, echoes the payload back via `ipc_send` on the reply port embedded in the request.
2. `ipc_pong_module` — owns port `P_PONG` (its reply port), is granted `CAP_IPC_SEND` for `P_PING`, calls `ipc_call(P_PING, req, reply)` with payload `"ping"`, asserts `reply.payload == "pong"` and `reply.payload_len == 4`.

Harness wiring:

- New `build/scripts/test_ipc_roundtrip.sh` (+ `.ps1` peer per #156) builds the two test modules into the kernel image and boots under QEMU via `run_qemu.sh`.
- Pass condition: serial log contains `TEST:PASS:ipc_roundtrip` (existing test framework macro convention from §3.3).
- Determinism: identical image hash across two consecutive runs (hooks into #174's image-hash gate once landed).

## Negative-path test (capability-denied)

Third in-kernel test module under `kernel/tests/ipc/ipc_deny_module`:

- Module is registered as a subject but is **not** granted `CAP_IPC_SEND` for `P_PING`.
- Calls `ipc_send(P_PING, msg)` and asserts return value is `IPC_ERR_CAP_DENIED`.
- Harness asserts serial log contains both:
  - the standard capability-denied marker per #164 (`CAP:DENY:<subject>:CAP_IPC_SEND`), and
  - `TEST:PASS:ipc_deny`.
- This satisfies §5.1's "unauthorized operation denied with an explicit error" validation bullet.

## Follow-up execute issues (sized for one agent session each)

1. **kernel/ipc: port table + `ipc_port_t` allocator (no message ops yet).** Lands the data structures, capability hooks, and unit tests for alloc/destroy.
2. **kernel/ipc: `ipc_send` + `ipc_recv` with single-waiter rendezvous.** Blocking semantics, no `ipc_call`. Includes positive unit test inside the kernel.
3. **kernel/ipc: `ipc_call` round-trip + reply-port encoding.** Builds on #2.
4. **cap: add `CAP_IPC_SEND` / `CAP_IPC_RECV` + cap_gate wrappers + audit coverage.** Locks the new IDs under `OS_ABI_VERSION` and updates `docs/abi/capability-handle.md`.
5. **tests: `ipc_ping_module` + `ipc_pong_module` + `test_ipc_roundtrip.sh` (+ `.ps1` peer).** Positive vertical slice; hooks into `validate_bundle.sh` TEST_TARGETS (avoid the #129 orphan regression).
6. **tests: `ipc_deny_module` + `test_ipc_deny.sh` (+ `.ps1` peer).** Negative-path; depends on #164 marker contract being merged.
7. **docs/abi: fill in `syscall.md` / `ipc-wire.md` / `capability-handle.md` / `manifest.md` sections for IPC v0.** Depends on #181 scaffold landing.

Issues 1 → 2 → 3 are strictly serial. Issue 4 can land in parallel with 1. Issues 5 and 6 depend on 1–4 + #164. Issue 7 depends on #181 and can land in parallel with 1–4.

## Exit criteria for M1 §5.1 (rolls up across the seven execute issues)

- `kernel/ipc/` exists with `ipc_port`, `ipc_msg`, and the three primitives implemented.
- `build/scripts/test_ipc_roundtrip.sh` and `build/scripts/test_ipc_deny.sh` pass under QEMU on both Linux and Windows hosts.
- `docs/abi/{syscall,ipc-wire,capability-handle,manifest}.md` document IPC v0 against `OS_ABI_VERSION`.
- No regressions in existing cap-audit / console / fs deny-path tests.
- Image hash determinism check still green (#174).
