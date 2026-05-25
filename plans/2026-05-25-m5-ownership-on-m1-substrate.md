# 2026-05-25 M5 Ownership graph + cascading deletion re-platformed onto merged M1 substrate (Plan)

**Status:** Plan-only (per #149 / #192). Implementation deferred to follow-up execute issues enumerated below.
**Tracks:** BUILD_ROADMAP §5.5 (M5: Ownership graph + cascading deletion).
**Owner:** kernel/cap + kernel/svc/broker_svc + launcher
**Last reviewed:** 2026-05-25
**Related:** #118 (canonical M5 plan, plan-only, closed), #241 (`cap_handle_revoke_subtree` stub reserved), #299 / #300 (M4-on-M1 plan), #302 / #303 / #304 / #305 (M4 substrate slices, all merged), #259 / #263 (M2-on-M1 plan), #276 / #277 (M3-on-M1 plan), #115 / #99 (broker core + acceptance tests), #220 / #229 / #246 (sync IPC v0 + handle-gated peers), #237 / #240 / #247 (cap_handle layer), #238 (process table), #249 (address_space carve-out), #254 (scheduler block/wake), #265 (canonical deny-marker emitter), #221 / #244 (CAP:DENY conformance + audit byte-fixture), #272 (console_svc), #282 (fs_svc), #287 (boot-order wiring), #273 / #289 (ipc_scratch handle handoff convention), #285 / #286 (manifest additive-enum precedent), #98 (audit wiring, gating the audit-deny SKIP markers), #311 (cascade audit gating, if filed).

## Motivation

BUILD_ROADMAP §5.5 ("M5: Ownership graph + cascading deletion") asks for two deliverables and two validations:

> Deliver:
> - ownership metadata and revocation logic
> - subtree deletion semantics
>
> Validate:
> - deleting Launcher removes owned app modules/resources
> - delegated caps derived from deleted owner become invalid

The canonical M5 design plan #118 (`plans/2026-05-13-ownership-graph-cascading-deletion.md`) is closed
as plan-only and proposes a broker-resident per-edge ledger plus a tombstone/eager-sweep
algorithm. That design predates the M1 substrate landing on `main` and predates the
M2/M3/M4 re-platforming trilogy (#259 / #276 / #299), so every assertion it sketches lives
in a single host process with hand-picked subject ids — none of the cascade or
tombstone paths ride on:

- the M1 process abstraction (`process_create` / `process_destroy`, #238),
- per-process `address_space_t` windows carved out of `.proc_arena` (#249),
- the cooperative scheduler block/wake plumbing (#254),
- the synchronous IPC v0 primitive (`ipc_send` / `ipc_recv` / `ipc_call`, #220 / #229),
- the handle-gated IPC peers (`ipc_send_h` / `ipc_recv_h`, #246),
- the 32-bit capability handle layer (#237 / #240 / #247),
- the in-kernel service-module precedent set by `console_svc` (#272), `fs_svc` (#282 / #287), and now `broker_svc` (#302).

With the M4 substrate trilogy now complete (#299 plan; #302/#303/#304/#305 implementation)
and `cap_handle_revoke_subtree` already reserved as a stub (#241), M5 is the next
roadmap domino and there are currently **zero open M5 execute issues**.

This plan is the **M5 analogue of #299 (M4)**, mirroring the proven shape from
#259 (M2) and #276 (M3): a plan-only doc that enumerates four PR-shaped execute
slices needed to re-platform the §5.5 validation bullets onto the merged M1
substrate, riding the `broker_svc` module landed by #302 and reusing the
`ipc_scratch[24..31]` broker-handle handoff convention landed by #303.

## Non-goals

- **Replacing #118's design.** That plan stays as the canonical data-model and
  audit-event reference; this plan is its execution-side companion and inherits
  the eager-sweep + lazy-invalidation strategy, edge shape, and per-edge +
  summary audit events verbatim where they remain compatible with the M1
  substrate (notably: kernel-resident edges live next to `broker_svc`'s
  existing `share_id → cap_handle_t` side-table, not in a userland ledger).
- **Adding host-fixture tests.** M5 has no merged host-fixture acceptance
  suite to mirror (#118 only sketched them; no PR landed). The two §5.5
  validation bullets therefore land directly as `_qemu` peers on the
  substrate, with no pre-flight host fixture — a deliberate departure from
  the M2/M3/M4 shape, where host fixtures pre-existed. Same discipline as
  greenfield substrate work: tests live where they are exercised.
- **Ownership transfer / reparenting.** #118 explicitly punts; this plan
  inherits that punt. M5 only deletes.
- **Cross-machine ownership.** Same punt as #118.
- **Quotas or storage accounting.** Same punt as #118.
- **GC of resources with no owner edge.** Cascade only follows recorded
  edges; orphaned resources stay where they are.
- **A new broker capability id.** `cap_broker_delete_owner` is gated on
  the existing subject-authority check (`actor_subject_id == owner`, or
  admin if CAP-013 is wired) — same shape `cap_broker_approve` uses today.
  No additions to `capability_id_t`. ABI-zero by default.
- **An `OS_ABI_VERSION` bump.** If the cascade audit emits a new reason
  code (`owner_deleted`) it is an additive string in the canonical deny
  emitter's reason table, not a schema change. Same discipline as M3's
  `CAP:DENY:<actor>:fs_read:<path>` strings.
- **Touching the marker grammar.** `docs/abi/capability-deny-contract.md`
  is unchanged; the cascade reuses the §4-compliant emitter (post-#265,
  conformance gated by #221 / #244).
- **A real interactive "delete this owner" UI.** Same shape as M4 approval:
  in v0 the request arrives via `ipc_send_h(broker_svc, BROKER_OP_DELETE_OWNER)`
  from a programmatic caller. Interactive prompts are deferred to §6.
- **Preemption, SMP, demand paging, ring transitions** — explicit non-asks
  carried over from #198 / #259 / #276 / #299.

## Explicit precondition

This plan deliberately does **not** depend on landing any new audit
wiring. The per-edge `cap.revoked.cascade` event and the summary
`cap.cascade.done` event from #118 ride on top of the existing audit
ring; if #98 (broker→audit wiring) hasn't landed yet at execute time, the
audit assertions emit `TEST:SKIP:m5_owner_delete_cascade_qemu:audit_cascade_recorded`
markers identical-in-shape to the M4 deny-audit SKIP from #304 — same
precondition discipline #276 / #299 used.

`cap_handle_revoke_subtree` is the one substrate primitive this plan
upgrades from "reserved stub" (#241) to "implements the §118 BFS".
Today `kernel/cap/cap_handle.c::cap_handle_revoke_subtree` returns
`CAP_ERR_CAP_INVALID` unconditionally and reserves the `parent_handle`
field on `cap_handle_row` as zero. Slice 1 below is the only place that
flips this behaviour; the rest of the plan rests on it.

## Design surface

### Topology after re-platforming

```
┌──────────────────────────────────┐
│  Owner App (process_create,      │
│  own address_space_t, subject=3) │
│  - holds CAP_FS_READ via         │
│    launcher_fs_grant_read        │
│  - holds broker send handle in   │
│    ipc_scratch[24..31] from #303 │
└──────────────────────────────────┘
                │
                │ ipc_send_h(broker_svc, BROKER_OP_REQUEST/APPROVE, ...)
                ▼
┌──────────────────────────────────┐    on every approve:
│  broker_svc (in-kernel, subject  │    1. cap_broker_approve         (existing)
│  = 5, landed via #302)           │    2. cap_handle_grant(recipient) (existing, from #304)
│  - per-share parent_handle:      │    3. row.parent_handle = owner_handle  (NEW, slice 1)
│    cap_handle_row.parent_handle  │
│  - BROKER_OP_DELETE_OWNER op     │    on delete_owner(owner_subj):
│    (NEW, slice 1)                │    1. broker_svc tombstones owner_subj
│                                  │    2. cap_handle_revoke_subtree(owner_root_handle)
│                                  │    3. process_destroy(owner_pid) → cap_handle_revoke_subject
│                                  │    4. emit one cap.revoked.cascade per revoked edge
│                                  │       + one cap.cascade.done summary (gated on #98)
└──────────────────────────────────┘
                │
                ▼
┌──────────────────────────────────┐
│  Recipient App (process_create,  │
│  own address_space_t, subject=4) │
│  - holds minted cap_handle_t     │
│    (subtree-revoked by slice 1)  │
└──────────────────────────────────┘
                ▲
                │ recipient's next ipc_send_h(fs_svc) returns IPC_ERR_CAP_DENIED
                │ via cap_gate_check_handle (subtree-revoked → CAP_ERR_REVOKED)
                ▼
┌──────────────────────────────────┐
│  Launcher (subject = 1) — issues │
│  BROKER_OP_DELETE_OWNER and      │
│  observes the cascade via the    │
│  broker reply + audit ring tail. │
└──────────────────────────────────┘
```

All actors (launcher, console-svc, fs-svc, broker-svc, owner, recipient)
are real M1 processes with their own PCB, address_space_t window, and
per-process IPC scratch buffer — exactly the topology #302/#303/#304/#305
already validate. M5 reuses that topology unchanged and only adds:

1. A `parent_handle` link on `cap_handle_row` (the field is **already
   reserved-as-zero** in #237's layout; this slice gives it semantics).
2. A real BFS walker in `cap_handle_revoke_subtree`.
3. A new broker op `BROKER_OP_DELETE_OWNER = 5` that drives the cascade.
4. Two `_qemu` test binaries + one `process_destroy`-recycle assertion.

### What changes (per surface)

1. **`cap_handle_revoke_subtree` real implementation** — flip
   `kernel/cap/cap_handle.{c,h}`'s v0 stub (#241) to a deterministic BFS:
   - On `cap_handle_grant(recipient, cap)` called by broker_svc, the
     caller passes a new `parent_handle` argument (default `CAP_HANDLE_NULL`
     for non-delegated grants from launcher); the broker_svc approve path
     passes the owner's own `cap_handle_t` for the broker port as the
     parent. The row's `parent_handle` field stops being reserved-as-zero
     and becomes a real edge.
   - `cap_handle_revoke_subtree(root)` walks `g_rows[]` in slot order
     (deterministic; `CAP_HANDLE_TABLE_MAX = 64` so this is O(64²)
     worst-case — well inside the M1 budget), finds every row whose
     `parent_handle` transitively reaches `root`, and calls the existing
     `cap_handle_revoke` on each (generation bump → handle denies). The
     walk is iterative with a `visited[64]` bitmap on stack (no heap, per
     `docs/CODING_CONVENTIONS.md` / #163).
   - The existing per-handle revoke contract (`cap_handle_revoke` →
     generation bump → `cap_gate_check_handle` returns `CAP_ERR_REVOKED`)
     is reused unchanged. Subtree-revoke is purely a fan-out around it.
   - **Backwards compatibility:** because `parent_handle = 0` is the
     pre-M5 invariant for every live row, calling
     `cap_handle_revoke_subtree(non_root_handle)` on a pre-M5 row set is
     a no-op (no rows have a non-null parent). M2/M3/M4 substrate tests
     continue to pass unchanged.
   - The `cap_handle_grant` signature extends additively. To stay
     source-compatible with #304's broker_svc call site that doesn't
     pass a parent, introduce a thin `cap_handle_grant_child(owner,
     cap, parent_handle)` and keep the existing `cap_handle_grant`
     as a one-line forwarder that passes `CAP_HANDLE_NULL`. Same
     discipline as the `cap_gate_check_handle` / `_result` split in
     #237.

2. **`broker_svc` ownership ledger + `BROKER_OP_DELETE_OWNER`** —
   extend `kernel/svc/broker_svc.{c,h}` (the module landed by #302):
   - Add the op enum value `BROKER_OP_DELETE_OWNER = 5` alongside the
     existing `BROKER_OP_REQUEST/APPROVE/DENY/REVOKE` from #302.
   - On `BROKER_OP_APPROVE`, after the existing `cap_broker_approve`
     + `cap_handle_grant`, broker_svc now also records the parent_handle
     by switching the grant call to `cap_handle_grant_child(recipient,
     cap, owner_handle_for_broker_port)`. The owner's broker-port handle
     is already in scope (it was used to drive the request via
     `ipc_send_h`); broker_svc reads it back out of the request message
     header (the same `ipc_scratch[24..31]` value, echoed in the
     request envelope per the M4 convention).
   - On `BROKER_OP_DELETE_OWNER {owner_subject_id}`:
     1. Authority check: `cap_broker_delete_owner_check(actor_subject,
        owner_subject)` — must be either `actor == owner` (self-delete)
        or admin (CAP-013 if wired, otherwise stubbed-allow with the
        same SKIP marker shape as #304's audit-deny). Reject with
        canonical `CAP:DENY:<actor>:cap_broker_delete_owner:<owner_id>`
        via the #265 emitter.
     2. Walk broker_svc's existing `share_id → cap_handle_t` side-table
        (capped at `CAP_BROKER_MAX_SHARES = 8` per #302) and, for every
        share whose owner subject matches, emit one
        `cap.revoked.cascade` audit event (gated on #98 → SKIP today).
     3. Call `cap_handle_revoke_subtree(owner_broker_port_handle)` — the
        single primitive that fan-outs to every delegated handle.
     4. Call `process_destroy(owner_pid)` — already calls
        `cap_handle_revoke_subject(owner_subject)` per #240 / verified
        live at `kernel/proc/process.c:204`. This is the belt-and-suspenders
        cleanup that catches any handles broker_svc didn't mint (e.g.
        launcher-issued grants that bypass the broker).
     5. Emit summary `cap.cascade.done {owner, n_children}` (SKIP today).
     6. Reply to the requester with `CAP_BROKER_OK` + cascade count.
   - **Ledger persistence is out of scope for M5 substrate.** #118 calls
     for an on-disk `broker/ownership.log` segment; that requires fs_svc
     persistence work (#282 landed read; persistence is partial). The
     v0 substrate slice keeps the ledger in-memory; tombstone persistence
     across reboot is filed as a follow-up (M5-SUBSTRATE-DOC). Lazy
     invalidation in-RAM still works because every cap-check goes through
     `cap_gate_check_handle`, which catches the generation bump from
     subtree-revoke regardless of whether the eager sweep "finished".

3. **Launcher slice** — extend `kernel/user/launcher.{c,h}`:
   - No new manifest fields. The launcher's existing
     `launcher_spawn_app_from_manifest` already mints the broker-port
     handle into `ipc_scratch[24..31]` per #303. The new
     `delete_owner` path is **launcher-initiated** in the test driver
     (the launcher process itself is the actor); no new mint, no new
     scratch byte range, no `docs/architecture/m1-m2-handoff.md`
     update. M5 is purely additive on the broker_svc side of the
     existing topology.
   - **Exception:** `tests/harness/svc_subjects.h` gains one constant
     `OWNER_SUBJECT_ID = 3` / `RECIPIENT_SUBJECT_ID = 4` if those
     aren't already exposed by the M4 harness (they likely are, since
     #304 needed them — slice 3 author confirms and reuses).

4. **Cap revocation on app exit** — same contract as M2's
   `launcher_console_revoke_restores_deny`, M3's
   `m3_fs_persist_deny_qemu`, and M4's
   `m4_broker_share_revoke_qemu:process_destroy_recycle_revokes_qemu`.
   The substrate makes it observable: `process_destroy(pid)` already
   calls `cap_handle_revoke_subject(subject)` (#240, verified live at
   `kernel/proc/process.c:204`). For M5 the assertion strengthens to:
   *after `BROKER_OP_DELETE_OWNER`, every previously-minted handle
   whose `parent_handle` transitively reaches the deleted owner's root
   fails `cap_gate_check_handle` with `CAP_ERR_REVOKED` (not
   `CAP_ERR_MISSING`)*. The error-code distinction is observable today
   in `cap_gate_check_handle_result` and is the proof-point that the
   cascade walked the tree rather than the bulk subject-revoke fired.

### Capability id reuse (no new id)

`BROKER_OP_DELETE_OWNER` reuses `CAP_IPC_SEND` for the port-gate (the
same choice slice 1 of M4 / #302 made for the broker-svc port — see
#299 §"Capability id for the broker-svc port"). The
authority distinction (`actor == owner` vs. admin) is enforced inside
`cap_broker_delete_owner_check`, exactly the way
`cap_broker_approve` already distinguishes approver-vs-owner. **No
addition to `capability_id_t` and no `OS_ABI_VERSION` bump.**

### Audit events (inheriting #118 §"Audit emission")

```
{ "kind": "cap.revoked.cascade",
  "owner": "<owner_subject_id>",
  "child_kind": "CAP",            // CAP only at M5; FILE / APP_MODULE
                                  //   land when fs_svc persistence and
                                  //   module unload wire in (post-M5)
  "child": "<cap_handle_t hex>",
  "reason": "owner_deleted",
  "seq": <u64> }

{ "kind": "cap.cascade.done",
  "owner": "<owner_subject_id>",
  "n_children": <u32>,
  "seq": <u64> }
```

Both events go through the existing `audit_emit_*` surface (#84 / #98).
Until #98 / cascade-audit wiring lands, the `audit_cascade_recorded_qemu`
sub-checks emit `TEST:SKIP:m5_owner_delete_cascade_qemu:audit_cascade_recorded`
with the same shape as M4 (#304).

### IPC scratch as initial-handoff vector

**Unchanged from #303.** M5 does not add a new scratch byte range.
The owner uses its existing `ipc_scratch[24..31]` broker-port handle
to issue `BROKER_OP_DELETE_OWNER`. The recipient never needs a new
handoff — its existing delegated `cap_handle_t` is the thing the
cascade revokes.

## ABI surface touched

- **None at the user/kernel boundary.** No additions to
  `docs/abi/syscalls.md`, `docs/abi/capabilities.md`,
  `docs/abi/ipc-wire.md`, or `docs/abi/manifest.md` required. No
  additions to `capabilities-registry.json`.
- **Internal-only:**
  - `cap_handle_grant_child` added as an additive companion to
    `cap_handle_grant` (kernel/cap/cap_handle.h). The existing
    `cap_handle_grant` keeps its signature and becomes a one-line
    forwarder; every M2/M3/M4 call site continues to compile.
  - `cap_handle_row.parent_handle` field stops being reserved-as-zero
    and becomes load-bearing. The struct layout is unchanged
    (#237's freeze still applies: `parent_handle` is a `uint32_t` and
    has always been part of the layout, see `kernel/cap/cap_handle.h:23`).
  - `kernel/svc/broker_svc.h` gains `BROKER_OP_DELETE_OWNER = 5` op
    enum value and `cap_broker_delete_owner_check` predicate.
  - `tests/harness/svc_subjects.h` reused as-is from #302/#304.

## Risks and explicit assumptions

- **Risk:** the BFS walker on a 64-row table is O(64²) in the worst case.
  *Mitigation:* `CAP_HANDLE_TABLE_MAX = 64` is the v0 cap; the M1
  scheduler tick budget tolerates ~4k operations per turn (per #254
  notes). 64² = 4096 row-comparisons fits in one turn with margin. If
  M5 needs to exceed 64 handles in a single subtree, that's a separate
  capacity-bump issue, not an M5 substrate one.
- **Risk:** `parent_handle` was previously zero for every row, so an
  off-by-one in the cascade walker could revoke unrelated handles
  whose `parent_handle == 0` (root handles).
  *Mitigation:* the walker treats `parent_handle == CAP_HANDLE_NULL`
  as a sentinel "no parent" terminator and does **not** treat
  matching-zero as a transitive hit. A dedicated test asserts that
  revoke-subtree on a leaf does not invalidate any sibling root.
- **Risk:** `process_destroy` already calls
  `cap_handle_revoke_subject` for the owner subject, which could mask
  bugs in the subtree walker by independently revoking the same rows.
  *Mitigation:* the test driver asserts the cascade revoke runs
  **before** `process_destroy` (the broker_svc loop does subtree
  revoke at step 3, process_destroy at step 4). The
  `subtree_revoked_before_destroy_qemu` sub-check uses
  `cap_gate_check_handle_result` to distinguish `CAP_ERR_REVOKED`
  (subtree) from `CAP_ERR_MISSING` (bulk subject revoke from
  process_destroy).
- **Risk:** broker_svc's `share_id → cap_handle_t` side-table can
  diverge from `cap_broker`'s internal `share_id → (owner, recipient,
  cap, resource)` table during cascade (cap_broker doesn't know about
  cascade today).
  *Mitigation:* the cascade walker on the broker_svc side enumerates
  *its own* side-table only; cap_broker's internal table is left
  untouched. The next legitimate `cap_broker_request_share` against
  a recycled subject id sees a clean state because subject ids are
  per-process and `process_destroy` already invalidates them.
- **Risk:** in-memory tombstones do not survive reboot, contradicting
  #118's persistence requirement.
  *Mitigation:* explicitly out of scope for the substrate slice (see
  §"What changes / 2.iv"). M5-SUBSTRATE-DOC follow-up tracks
  `broker/ownership.log` persistence once fs_svc write+sync (#282
  follow-ups) lands. The substrate slice covers single-boot semantics
  — sufficient to satisfy both §5.5 validation bullets on `_qemu`.
- **Risk:** a third process (not owner, not admin) issuing
  `BROKER_OP_DELETE_OWNER` could either (a) crash broker_svc or
  (b) silently no-op without an audit trail.
  *Mitigation:* `cap_broker_delete_owner_check` rejects with a
  canonical `CAP:DENY:<actor>:cap_broker_delete_owner:<owner_id>`
  string via #265's emitter; conformance gated by #221 / #244. A
  dedicated `bystander_cannot_delete_owner_qemu` sub-check asserts
  the deny path emits the canonical string and leaves all minted
  handles untouched.
- **Assumption:** all of #220, #229, #237, #240, #246, #247, #249,
  #254, #265, #268..#275 (M2 substrate), #278..#282, #287..#291
  (M3 substrate), #302..#305 (M4 substrate), and #241
  (`cap_handle_revoke_subtree` stub) are on `main` at execute time.
  All verified at plan-write time @ `5ed2f57` (per #181's last status
  comment) modulo any in-flight rebases.

## Acceptance demo (maps directly to §5.5 validation bullets)

Each §5.5 validation bullet lands as a `_qemu` peer on the substrate.
There is no pre-flight host fixture (M5 is greenfield; see
§"Non-goals / Adding host-fixture tests").

| §5.5 validation bullet | Substrate-ridden `_qemu` test | M1 primitive exercised |
| --- | --- | --- |
| "deleting Launcher removes owned app modules/resources" | `TEST:PASS:m5_owner_delete_cascade_allow_qemu` | `process_create` (#238) + `cap_handle_grant_child` (slice 1) + `BROKER_OP_DELETE_OWNER` (slice 1) + `cap_handle_revoke_subtree` (slice 1) → owner's minted handles all return `CAP_ERR_REVOKED` |
| "delegated caps derived from deleted owner become invalid" | `TEST:PASS:m5_owner_delete_cascade_allow_qemu:delegated_caps_invalid` | recipient's `ipc_send_h(fs_svc, ..., revoked_handle)` returns `IPC_ERR_CAP_DENIED` via `cap_gate_check_handle` (#237) with canonical `CAP:DENY:<recipient>:fs_read:<path>` from #265 |
| (bystander deny) | `TEST:PASS:m5_owner_delete_cascade_deny_qemu:bystander_cannot_delete_owner` | non-owner, non-admin `BROKER_OP_DELETE_OWNER` returns `CAP_BROKER_ERR_PERMISSION`; canonical deny string emitted |
| (idempotence) | `TEST:PASS:m5_owner_delete_cascade_deny_qemu:double_delete_is_idempotent` | second `BROKER_OP_DELETE_OWNER` on the same owner returns `CAP_BROKER_OK` (no-op); minted handles still denied |
| (recycle) | `TEST:PASS:m5_owner_delete_cascade_deny_qemu:process_destroy_recycle_revokes_qemu` | re-running `process_create` for the same subject id starts deny-by-default; old handles continue to deny with `CAP_ERR_REVOKED` |
| (ordering — substrate-only) | `TEST:PASS:m5_owner_delete_cascade_allow_qemu:subtree_revoked_before_destroy_qemu` | `cap_gate_check_handle_result` returns `CAP_ERR_REVOKED` (not `CAP_ERR_MISSING`) immediately after `BROKER_OP_DELETE_OWNER` reply, proving subtree walker fired before `process_destroy` |
| (audit, deferred to #98) | `TEST:SKIP:m5_owner_delete_cascade_allow_qemu:audit_cascade_recorded` | gated on #98 / cascade-audit wiring; identical SKIP-marker shape to #304's `audit_deny_recorded_qemu` |
| (audit summary, deferred to #98) | `TEST:SKIP:m5_owner_delete_cascade_allow_qemu:audit_cascade_done_recorded` | same gating; one summary event per `BROKER_OP_DELETE_OWNER` |

All `_qemu` markers wire into `build/scripts/test.sh` and
`build/scripts/test.ps1` with `.shell_parity_allowlist` entries per
#156, and surface in the validator JSON report (#110) the same way
M1/M2/M3/M4 substrate markers already do.

## Follow-up implementation issues to file

These execute issues are the concrete units of work this plan
unblocks. Each is sized for one PR / one agent session.
Proposed titles + done-when bullets:

1. **"M5-SUBSTRATE-001: real `cap_handle_revoke_subtree` BFS walker + `cap_handle_grant_child` parent-handle plumbing (BUILD_ROADMAP §5.5)"**
   - Flips `cap_handle_revoke_subtree` from the #241 `CAP_ERR_CAP_INVALID`
     stub to a deterministic slot-order BFS over `g_rows[]`.
   - Adds `cap_handle_grant_child(owner, cap, parent_handle)`;
     `cap_handle_grant` becomes a forwarder that passes
     `CAP_HANDLE_NULL`. All existing M2/M3/M4 call sites compile
     unchanged.
   - Validator targets: a unit test `cap_handle_revoke_subtree_walks`
     proves a 3-level chain revokes via a single subtree-revoke call;
     a regression test `cap_handle_revoke_subtree_null_parent_no_op`
     proves rows with `parent_handle == 0` are not transitively
     revoked when an unrelated root is the cascade root; the existing
     `cap_handle_revoke_subtree_null_handle_invalid` test from #241
     continues to pass (`CAP_HANDLE_NULL` → `CAP_ERR_CAP_INVALID`
     unchanged).
   - No new ABI surface; `cap_handle_row.parent_handle` field is
     already reserved-as-zero in #237's frozen layout.

2. **"M5-SUBSTRATE-002: `broker_svc` `BROKER_OP_DELETE_OWNER` op + cascade orchestration (BUILD_ROADMAP §5.5)"**
   - Extends `kernel/svc/broker_svc.{c,h}` with op enum value
     `BROKER_OP_DELETE_OWNER = 5`, the
     `cap_broker_delete_owner_check` authority predicate, and the
     six-step cascade orchestration (tombstone → audit per-edge SKIP →
     `cap_handle_revoke_subtree` → `process_destroy` → audit summary
     SKIP → reply).
   - Switches the existing `BROKER_OP_APPROVE` cap_handle_grant call
     to `cap_handle_grant_child` so the per-share row records its
     parent. This is the load-bearing flip that gives the slice 1
     walker a real graph to walk.
   - Validator targets: `broker_svc_delete_owner_authority_check`
     unit test (host fixture); `broker_svc_cascade_revokes_minted_handle`
     unit test; canonical deny string emission asserted via #221
     conformance.
   - No new ABI surface (op enum is internal to `broker_svc.h`).

3. **"M5-SUBSTRATE-003: `m5_owner_delete_cascade_allow_qemu` substrate test (BUILD_ROADMAP §5.5)"**
   - Lands `tests/m5_owner_delete_cascade_allow_qemu_test.c` covering
     the four PASS sub-checks under the `allow` umbrella from the
     table above (`m5_owner_delete_cascade_allow_qemu`,
     `:delegated_caps_invalid`, `:subtree_revoked_before_destroy_qemu`)
     plus the two `audit_cascade_*` SKIP markers.
   - Wires into `build/scripts/test.sh` + `build/scripts/test.ps1`
     with `.shell_parity_allowlist` entry per #156.
   - Surfaces in validator JSON report (#110) under `m5_ownership`.

4. **"M5-SUBSTRATE-004: `m5_owner_delete_cascade_deny_qemu` substrate test + recycle assertion (BUILD_ROADMAP §5.5)"**
   - Lands `tests/m5_owner_delete_cascade_deny_qemu_test.c` covering
     the three PASS sub-checks under the `deny` umbrella
     (`bystander_cannot_delete_owner`,
     `double_delete_is_idempotent`,
     `process_destroy_recycle_revokes_qemu`).
   - Same test.sh / test.ps1 / `.shell_parity_allowlist` /
     validator-report wiring as slice 3.

A separate, non-blocking housekeeping issue
(**M5-SUBSTRATE-DOC**) covers two post-substrate items:

- The eventual on-disk `broker/ownership.log` tombstone-persistence
  segment from #118, once fs_svc write+sync (#282 follow-ups) lands.
- An additive `manifests/schema/v0.json` extension for an optional
  `capabilities.ownership_role: "owner" | "recipient"` enum (mirrors
  #285 / #286's `persistence` enum discipline), so the launcher could
  refuse to mint the broker handle on the wrong side. Both are
  out-of-scope for this plan because schema and persistence changes
  need their own versioning review.

## Out of scope (explicit non-asks)

- Cross-machine ownership (carried over from #118).
- Reparenting / ownership transfer (carried over from #118).
- Quotas or storage accounting (carried over from #118).
- GC of resources with no recorded owner edge (carried over from #118).
- A new `CAP_BROKER_DELETE_OWNER` capability id (existing
  subject-authority check via `cap_broker_delete_owner_check`).
- A real interactive "delete this owner?" prompt UI (deferred to §6
  TTY console_svc surface).
- `audit_cascade_recorded` / `audit_cascade_done_recorded` markers
  being upgraded from SKIP to PASS — that's #98 / cascade-audit
  wiring, unchanged by this plan.
- Persistent tombstones across reboot — substrate-slice keeps the
  ledger in-memory; persistence is M5-SUBSTRATE-DOC follow-up.
- Mirroring into a real QEMU ISO boot — the `_qemu` suffix denotes
  "rides on the real M1 substrate", matching the precedent set by
  `m1_ipc_demo_test.c` and the merged M2 / M3 / M4 substrate slices.
- `FILE` and `APP_MODULE` `child_kind` audit values — M5 substrate
  emits `CAP` only; the broader inventory lands when fs_svc
  persistence and module unload primitives catch up to the cascade
  surface (post-M5).
- The marker grammar widening conversation (#260 / #261 both closed;
  this plan reuses the canonical emitter unchanged).
- Preemption, SMP, demand paging, ring transitions — explicit
  non-asks carried over from #198 / #259 / #276 / #299.

_Filed by hourly issue-work cron 2026-05-25 — fills the gap between
the merged M4 substrate trilogy (#302/#303/#304/#305) and BUILD_ROADMAP
§5.5's two ownership-cascade validation bullets, opening the M5
execute-issue queue with four sized slices._
