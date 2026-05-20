# 2026-05-20 M1 Kernel Capability Table Skeleton (Plan)

**Status:** Plan-only (per #193). Implementation deferred to follow-up execute issues enumerated below.
**Tracks:** BUILD_ROADMAP §5.1 (M1 minimal kernel isolation + IPC skeleton); informs §5.4 (M4 broker), §5.5 (M5 ownership cascade), §7 (ABI freeze).
**Owner:** kernel
**Last reviewed:** 2026-05-20
**Related:**
- #150 (`OS_ABI_VERSION=0` anchor / handle representation)
- #164 (capability-denied error + log marker contract, landed via #167)
- #180 / #185 (M1 synchronous IPC primitive plan — sender/receiver checks)
- #192 (M1 process abstraction + address-space boundary plan)
- #115 (M4 broker allow/deny/revoke) and #118 / `2026-05-13-ownership-graph-cascading-deletion.md` (M5 ownership cascade)
- #163 / #166 (kernel/module/user-lib boundary conventions)
- `docs/abi/capabilities.md`, `docs/abi/manifest.md`, `docs/abi/ipc-wire.md`

## Motivation

BUILD_ROADMAP §5.1 lists three M1 deliverables:

1. process abstraction + address-space boundary (#192),
2. synchronous IPC primitive (#180, plan landed in #185),
3. **kernel capability table skeleton** (this plan).

The in-tree capability code today (`kernel/cap/cap_table.{c,h}`, `cap_gate.{c,h}`, audit/checkpoint chain) is a flat per-subject grant set built before there were processes or IPC ports to bind capabilities to. It satisfies the M2 console slice (#87, audit/deny work under #84/#164) but it does **not** yet:

- distinguish a kernel-resident handle from an audit/policy entry,
- bind a capability to the process / address space introduced under #192,
- expose a wire-stable handle representation as called for by §7 and #150,
- supply revocation hooks the M4 broker (#115) and M5 ownership cascade (#118 / `plans/2026-05-13-ownership-graph-cascading-deletion.md`) need.

This plan locks the **smallest** kernel-resident table that satisfies §5.1, frozen against `OS_ABI_VERSION=0`, so the M1 IPC primitive (#180 / #185) and the M1 process boundary (#192) have a stable surface to gate on.

## Non-goals

- Delegation graph or transitive grants (M4/M5 territory; only a `parent_handle` field is reserved in the v0 row, not enforced).
- User-space syscall surface for `grant` / `revoke` (still launcher-mediated until M4; see #87).
- Multi-process broker logic, share workflow, audit-driven revocation (defer to #115).
- Cross-machine / persisted capability handles (the on-disk manifest under `docs/abi/manifest.md` is the only declaration path; runtime handles are RAM-only in v0).
- Replacing the existing audit chain (`cap_audit_*`) — this plan **wraps** it, not replaces it.

## Design surface

### Table data structure: per-process slots backed by a global handle table

Two complementary structures, both kernel-owned, both in `kernel/cap/cap_handle.{c,h}` (new file, sibling to existing `cap_table.{c,h}`):

```
struct cap_handle_row {
  uint16_t        abi_version;       // == OS_ABI_VERSION (#150)
  uint16_t        flags;             // bit0 = live, bit1 = revoked, bit2 = sealed; rest MBZ in v0
  capability_id_t cap_id;            // CAP_CONSOLE_WRITE … CAP_NETWORK (capability.h)
  cap_subject_id_t owner_subject;    // process / module subject this row authorizes
  cap_subject_id_t granter_subject;  // who installed the row (launcher = subject 1 today)
  uint32_t        parent_handle;     // 0 in v0 (delegation graph reserved for M4/M5)
  uint32_t        generation;        // monotonic per-slot, incremented on free/revoke
};
```

- Global table: `cap_handle_row rows[CAP_HANDLE_TABLE_MAX]` (`CAP_HANDLE_TABLE_MAX = 64` in v0; small enough for the M2 console + M3 fs + M4 broker fixtures, bounded so it fits a fixed `.bss` page on the M1 freestanding kernel).
- Per-process slot view: each process (#192) carries a small `uint16_t handle_ids[CAP_PROCESS_HANDLE_MAX]` (8 in v0) indexing into the global table. The slot view is what syscall entry consults; the global table is what the broker (#115) and ownership cascade (#118) walk.
- Tables are statically allocated. No `kmalloc` dependency at the M1 layer (matches the no-heap policy in `docs/CODING_CONVENTIONS.md` / #163).

### Handle representation (ABI-frozen, cross-refs #150)

The handle exposed to syscalls and reified in IPC envelopes (#185) is a 32-bit value:

```
bits  0..15  : slot index into the global cap_handle_row table
bits 16..29  : generation counter (low 14 bits of row.generation)
bits 30..31  : tag = 0b01 ("kernel capability handle"); other tags reserved
```

Properties:

- Stale handles (after revoke or owner exit) fail the generation check at gate entry and produce a `CAP_ERR_MISSING` with the existing `CAP_AUDIT:` marker (#164 / #167).
- Layout is documented in `docs/abi/capabilities.md`; this plan does **not** edit that doc — the execute issue does, gated on #150 landing.
- Tag bits exist so future kinds (file handles, IPC port handles per #185, broker tickets per #115) can share the 32-bit handle namespace without colliding.

### Mapping the M2 console grants into the table

The M2 console slice (#87, helloapp under #92/#100) currently calls `cap_table_grant(subject, CAP_CONSOLE_WRITE)` directly from the launcher. The M1 table skeleton keeps that surface API-compatible, but `cap_table_grant` becomes a thin façade that:

1. Allocates a `cap_handle_row` for `(subject, cap_id)` if one is not already live,
2. Records `granter_subject = launcher_subject_id`, `generation++`, sets `flags.live`,
3. Forwards the existing `cap_audit_record(GRANT, …)` call (#84/#167) unchanged.

`cap_table_check(subject, cap_id)` is rewritten on top of the row lookup but keeps its current return signature. Existing tests (`tests/launcher_console_test.c`, `tests/cap_audit_*`) MUST keep passing without source edits — this is the migration acceptance gate.

### Syscall-entry check shape

There is no userspace syscall ABI at M1 — kernel modules call gates directly — so the "syscall entry" today is a function call. The plan freezes the shape so the future M1 process boundary (#192) can drop a real syscall trampoline in front of it without ABI churn:

```
cap_result_t cap_gate_check_handle(
    uint32_t          handle,       // ABI handle defined above
    cap_subject_id_t  caller,       // from current process descriptor (#192)
    capability_id_t   expected_cap, // what the gate site requires
    const char       *site_tag);    // for the deny log marker (#164/#167)
```

Behaviour:

- Resolves slot+generation, returns `CAP_ERR_CAP_INVALID` if tag bits ≠ `0b01`, `CAP_ERR_MISSING` if slot/gen stale or revoked.
- On deny: emits the exact `CAP_AUDIT:` line shape documented in `docs/abi/capability-deny-contract.md` (#167) — `op=CHECK`, `result=MISSING`, `outcome=DENY`, plus `site=<site_tag>` once that field is landed under #167's follow-up.
- On allow: returns `CAP_OK` without an audit line (the audit chain only logs grants/revokes/denies; allows are inferred, matching current behaviour).

### Revocation hooks (for M4 broker / M5 ownership cascade)

The table exposes three kernel-internal hooks the M4 broker (#115) and the M5 ownership cascade (#118 / `plans/2026-05-13-…`) will use, all of which are reserved (declared, unimplemented) at M1:

- `cap_handle_revoke(uint32_t handle, cap_subject_id_t revoker)` — sets `flags.revoked`, bumps `generation`, audits `REVOKE`. Called by `cap_table_revoke` today; M4 broker will call it directly.
- `cap_handle_revoke_subject(cap_subject_id_t owner)` — bulk-revoke on process exit (#192). Iterates the global table once; O(CAP_HANDLE_TABLE_MAX).
- `cap_handle_revoke_subtree(uint32_t parent_handle)` — reserved, returns `CAP_ERR_CAP_INVALID` until M4/M5 wire the delegation graph; presence on day one keeps the symbol stable.

All three feed the existing audit chain (#84/#167) and respect the checkpoint cadence already enforced by `CAP_AUDIT_CHECKPOINT_INTERVAL`.

### Interaction with the IPC primitive (#180 / #185)

`plans/2026-05-19-m1-sync-ipc-primitive.md` requires `recv_cap` and `send_cap` checks on every port operation, and pins `ipc_msg_v0.abi_version == OS_ABI_VERSION`. The handle representation above is the single 32-bit value those checks will accept. Concretely:

- `ipc_send(port, msg)` and `ipc_recv(port, msg)` call `cap_gate_check_handle` with the caller's currently-installed handle for `send_cap` / `recv_cap`.
- The IPC port table (per #185) stores `capability_id_t`, not a handle — handles are per-caller, the port-side requirement is per-cap-kind. This plan is explicit on that split so the two M1 plans do not collide on representation.

### Interaction with the process boundary (#192)

The M1 process plan (#192) introduces the `process_t` / address-space descriptor. The capability table piggybacks on it by storing the `handle_ids[]` slot view in the process descriptor rather than a separate kernel-global per-subject map. On process exit, `process_destroy()` calls `cap_handle_revoke_subject(process->subject_id)`. This plan declares that contract; the execute issue under #192 owns the implementation.

## Migration / compatibility

- `cap_table.{c,h}` keeps its current header signatures. The `.c` is rewritten on top of `cap_handle.{c,h}`; callers do not change.
- Existing audit format (`CAP_AUDIT:seq=…`) is unchanged — the table change MUST NOT alter a single byte of any audit line under the current M2 fixture set. This is the regression gate the execute issue will assert with a fixture-diff test.
- The 32-bit handle is **new ABI surface**, frozen against `OS_ABI_VERSION=0` (#150). Any change to the bit layout above is a v0→v1 break and follows §7 of `docs/abi/manifest.md` / the `OS_ABI_VERSION` bump path.
- No on-disk format is introduced. Handles are RAM-only.

## Acceptance criteria for the execute issue(s)

When the implementation lands, it must:

1. Add `kernel/cap/cap_handle.{c,h}` with the row struct, table, and the four functions above (`cap_gate_check_handle`, `cap_handle_revoke`, `cap_handle_revoke_subject`, `cap_handle_revoke_subtree`).
2. Re-implement `cap_table_grant` / `cap_table_revoke` / `cap_table_check` as thin façades; **all existing tests pass without source edits** (`build/scripts/test.sh` matrix, especially `tests/launcher_console_test.c`, `tests/cap_audit_*`, `tests/codesign_test.c`).
3. Update `docs/abi/capabilities.md` with the 32-bit handle layout (gated on / coordinated with #150 / #158).
4. Add a unit test that proves the generation check: grant → handle → revoke → same numeric handle now denies with `CAP_ERR_MISSING` and emits the deny marker (#167).
5. Add a unit test for `cap_handle_revoke_subject` covering bulk revoke of three caps for one owner without disturbing other owners' rows.
6. No on-disk artifact changes; no manifest schema bump; no IPC code in this issue (IPC integration is the §5.1 IPC execute issue, which depends on this one).
7. Deterministic build / image-hash unchanged for unrelated artefacts (#174 image-hash check, once landed).

## Proposed follow-up implementation issues

To be filed once #193 is approved (one execute issue per row keeps PRs reviewable):

- **M1-CAPTBL-001** — Land `kernel/cap/cap_handle.{c,h}` with row struct, global table, and slot view; no behaviour change for existing call sites. (Acceptance #1, #2.)
- **M1-CAPTBL-002** — Wire 32-bit handle representation and `cap_gate_check_handle`; freeze layout against `OS_ABI_VERSION=0` (depends on #150 / #158). (Acceptance #3, #4.)
- **M1-CAPTBL-003** — Implement `cap_handle_revoke_subject` + process-exit hook stub; gated on #192 landing. (Acceptance #5.)
- **M1-CAPTBL-004** — Declare `cap_handle_revoke_subtree` (unimplemented, returns `CAP_ERR_CAP_INVALID`); reserves the symbol for #115 / #118.
- **M1-CAPTBL-005** — Fixture-diff test asserting byte-exact `CAP_AUDIT:` lines under the M2 console fixture set, pinning audit-format stability through the table rewrite.
- **M1-CAPTBL-006** — IPC integration glue: `ipc_send` / `ipc_recv` consume handles via `cap_gate_check_handle`. Depends on #180 execute issue *and* M1-CAPTBL-002.

## Risks

- **Handle aliasing.** A 14-bit generation field eventually wraps. v0 mitigates by treating wrap as a fatal kernel assert (handles are short-lived in M1; the M4 broker plan owns the long-lived case). Widening generation is an `OS_ABI_VERSION` bump.
- **Static-table sizing.** `CAP_HANDLE_TABLE_MAX = 64` is enough for M1–M3 fixtures but will need a bump (and an ABI re-freeze decision) before M4 broker scenarios. Tracked here, not changed here.
- **Façade compatibility.** Subtle behavioural drift in `cap_table_check` could re-order audit events. Acceptance #2 + fixture-diff test (M1-CAPTBL-005) is the guard.
- **Cross-plan ordering.** This plan, #180/#185, and #192 are mutually referential. The execute graph above keeps M1-CAPTBL-001 free of the other two; only M1-CAPTBL-003 (#192) and M1-CAPTBL-006 (#180) introduce hard dependencies.

## References

- BUILD_ROADMAP §5.1 (M1 deliverables), §5.4 (M4 broker), §5.5 (M5 ownership), §7 (ABI freeze plan)
- `docs/abi/capabilities.md` (existing kernel-side cap vocabulary)
- `docs/abi/capability-deny-contract.md` (#164 / #167 deny-marker format)
- `docs/abi/manifest.md` (#183 / #187 — requested-capability declarations)
- `plans/2026-05-19-m1-sync-ipc-primitive.md` (#180 / #185)
- `plans/2026-05-13-ownership-graph-cascading-deletion.md` (#118)
- `kernel/cap/capability.h`, `kernel/cap/cap_table.{c,h}`, `kernel/cap/cap_gate.{c,h}` (current in-tree surface)
