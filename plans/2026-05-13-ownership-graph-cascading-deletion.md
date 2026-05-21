# 2026-05-13 M5 Ownership Graph + Cascading Deletion Slice

## Goal
Deliver the M5 vertical slice from BUILD_ROADMAP §5.5: a small, testable
ownership graph that records which principal owns which delegated resource or
capability, plus a cascading-deletion algorithm that revokes the entire owned
subtree when a parent is deleted. This is the milestone where capability
*revocation* — which the audit/sequence work in CAP-016..020 was built to
support — stops being theoretical.

Restating the roadmap deliverables:

> Deliver:
> - ownership metadata and revocation logic
> - subtree deletion semantics
>
> Validate:
> - deleting Launcher removes owned app modules/resources
> - delegated caps derived from deleted owner become invalid

This plan is a *plan only*. The acceptance tests below are the contract a
follow-up "Execute plan" issue will implement; no code lands as part of #118.

## Scope
- Keep deny-by-default semantics for console (#82), filesystem (#83), broker
  (#85), and audit (#84) slices unchanged.
- Add an ownership edge for every delegated resource the broker or launcher
  hands out, so revocation has a graph to walk.
- Define one cascading-deletion algorithm that works uniformly for app modules,
  files owned by an app, and capabilities delegated by an app.
- Keep the new ABI surface minimal: one `delete_owner(owner_id)` entry point
  on the broker, plus the per-edge metadata required to walk the graph.

Out of scope:

- Cross-machine ownership (no remote owner edges).
- Garbage collection of resources that have *no* owner edge — those stay where
  they are; cascade only follows recorded edges.
- Reparenting / ownership transfer. M5 only deletes; transfer is a later slice.
- Quotas or storage accounting.

## Dependencies
- #85 (capability broker + share workflow, PR #99) — broker contracts decide
  where ownership edges live; this plan assumes the broker ledger is the
  canonical store. Land #85 first.
- #84 (capability audit + deny log, PR #98) — cascade emits structured audit
  events through this surface; reuses the `TEST:PASS:` / JSON shape from #110.
- #93 (ABI reference docs) — any new manifest/ownership surface introduced
  here must be added to the ABI doc when execution lands.
- CAP-013 (admin gate), CAP-014 (non-delegable admin grant),
  CAP-015 (actor attribution), CAP-016..020 (audit sequence/checkpoint) —
  reused as-is. No extension expected; if the cascade needs a new audit event
  type (`cap.revoked.cascade`) it should be added as a new reason code, not a
  schema change.

## Data Model

### Where ownership edges live
**Decision: edges live in the capability broker ledger.** Justification:

- The broker is already the single point that mints delegated capabilities
  (#85), so it sees every grant the moment it happens — the natural place to
  also record `(parent_owner, child_resource)`.
- The cap table in the kernel is hot-path; adding a back-pointer per cap entry
  bloats it and forces revocation to scan every process. Keeping the graph in
  the broker keeps the kernel hot path unchanged.
- The manifest registry is static (declared at install time) and does not see
  *runtime* delegations such as broker-issued shares, so it cannot be the sole
  store.

The kernel cap table keeps a small `owner_id` field (already present per
CAP-015 actor attribution); the broker ledger keeps the full graph and is the
authority for "is this owner still alive."

### Edge shape
A single edge type, written once at grant time, never mutated:

```
struct ownership_edge {
    owner_id_t   owner;        // parent principal (e.g. Launcher)
    resource_t   kind;         // APP_MODULE | FILE | CAP
    resource_id_t child;       // stable id of the owned thing
    cap_id_t     via_cap;      // the delegated cap that proves the link (or 0)
    uint64_t     seq;          // audit sequence at grant time (CAP-016)
};
```

Persistence: the broker ledger is already persisted to the faux FS slice
(#83). Edges are appended to a new `broker/ownership.log` segment using the
same append-only format as the audit ring; on boot the broker replays the log
into an in-memory adjacency list.

### Lookups required
- `children_of(owner_id) -> [edge]` — used by cascade.
- `is_alive(owner_id) -> bool` — used at cap-check time to invalidate caps
  whose owner has been deleted. Backed by a small "tombstone" set so deleted
  owners stay deleted across reboots.

## Revocation Algorithm

**Strategy: eager sweep on delete, lazy invalidation on cap use.** Both,
deliberately:

- The eager sweep walks `children_of(owner)` in BFS order, deletes each child
  resource (file → unlink via FS service; cap → revoke in cap table; app
  module → unmap + unload), and recurses into any child that is itself an
  owner. This gives the visible "deleting Launcher removes owned app modules"
  behavior the roadmap calls for, and bounds work to the subtree size.
- Lazy invalidation is the safety net: every cap-check first asks
  `broker.is_alive(cap.owner_id)`. If the owner has been tombstoned,
  the cap denies regardless of whether the eager sweep already reached it.
  This makes the cascade crash-safe — if we power-cycle mid-sweep, no
  delegated cap from a deleted owner can succeed on the next boot, because
  the tombstone is persisted before the sweep starts.

Order of operations in `delete_owner(owner_id)`:

1. Append `tombstone(owner_id, seq)` to the broker ledger and `fsync`.
   *After this point, every subsequent cap-check for this owner denies.*
2. BFS the ownership subtree, in deterministic id order.
3. For each child: emit one structured audit event, then perform the
   resource-specific deletion. Failures are logged but do not abort the
   sweep (the tombstone already guarantees deny).
4. Append `cascade_done(owner_id, seq, n_children)` to the audit ring.

### Audit emission
**One event per revoked edge, plus one summary event.** Per-edge events are
required by the roadmap's "delegated caps derived from deleted owner become
invalid" line — auditors need to see *which* caps were invalidated, not just
a count. The summary event lets log consumers detect a partial sweep
(`cascade_done` missing ⇒ crashed mid-cascade ⇒ tombstone is the only
guarantee).

Event shape reuses the #84 record:

```
{ "kind": "cap.revoked.cascade",
  "owner": "<owner_id>",
  "child_kind": "FILE|CAP|APP_MODULE",
  "child": "<resource_id>",
  "reason": "owner_deleted",
  "seq": <u64> }
```

Summary event uses `kind: "cap.cascade.done"` with `n_children` and the
`seq` of the originating tombstone.

## Acceptance Tests

All tests follow the `TEST:PASS:` / structured JSON-report contract from #110
and live under `tests/m5_ownership/`. Each runs in QEMU off the deterministic
disk image.

### `ownership_cascade_delete`
- Boot, start Launcher.
- Launcher loads two app modules (`AppA`, `AppB`) and writes one file per app
  via the FS service. Each load/write goes through the broker, so three
  ownership edges are recorded under `Launcher`.
- Test driver calls `broker.delete_owner(Launcher)`.
- Assert: both app modules are unloaded, both files are gone from the FS
  service's namespace, and `broker.children_of(Launcher)` is empty.
- Assert: a fresh `broker.is_alive(Launcher)` returns false after reboot
  (tombstone persisted).

### `ownership_cascade_cap_invalidate`
- Same setup, but before deleting Launcher the test grabs a delegated
  console-write cap that `AppA` previously received from Launcher.
- Call `delete_owner(Launcher)`.
- Use the cached cap to attempt a console write.
- Assert: the cap-check denies with reason `owner_deleted`, and a deny
  audit record is emitted (proving lazy invalidation works even if the
  eager sweep had not yet reached this cap — simulate by inserting a
  yield between tombstone-write and the BFS in a debug build).

### `ownership_audit_event`
- Run `ownership_cascade_delete` again.
- Read the audit ring after the cascade.
- Assert: exactly N `cap.revoked.cascade` records (one per owned edge,
  matching the setup count), followed by exactly one `cap.cascade.done`
  with the same `n_children`.
- Assert: monotonic `seq` ordering across all emitted records (CAP-016).

Each test exits with `TEST:PASS:m5_ownership/<name>` on success, and the
JSON validator report (#110) gains an `m5_ownership` section.

## Validation Strategy
- `build/scripts/test.sh m5_ownership` runs the three tests against the
  deterministic image; CI gates on the JSON report.
- No new build flags. No new top-level scripts. The slice reuses the
  `tools/sof_wrap` packaging path so the executable-bit issues fixed by
  #101 / #90 / #91 don't recur.
- Negative coverage: the deny-path tests from #92 (#100) and #108 must
  continue to pass unchanged, proving the cascade does not relax any
  existing deny.

## Open Questions for the Execute Issue
- Tombstone GC policy: do we ever forget a deleted owner? Proposal: never,
  for the M5 slice. Tombstones are tiny.
- Should `delete_owner` itself require an admin cap (CAP-013)? Proposal:
  yes — only the principal that minted an owner edge, or admin, may delete
  the owner. Out of scope to *implement* in M5 if the broker already gates
  all writes by admin; record the decision either way.

## Exit Criteria
- A follow-up "Execute plan: M5 ownership graph + cascading deletion" issue
  is filed referencing this plan.
- The three acceptance tests above are agreed as the merge gate for that
  execute issue.
- No code changes land in this issue (#118).

## Notes
- Plan length kept ≤ ~250 lines per the issue body's request; deeper design
  (eager-vs-lazy proofs, on-disk format details) belongs in a follow-up
  ADR rather than this plan.
- This plan deliberately does not extend any CAP-* commit; if execution
  reveals an ABI gap, file a separate issue rather than widening M5.
