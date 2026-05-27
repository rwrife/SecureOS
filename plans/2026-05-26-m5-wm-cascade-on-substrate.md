# 2026-05-26 M5 window-manager session teardown participates in cascading deletion (Plan)

**Status:** Plan-only (per #149 / #192). Implementation deferred to the
follow-up execute slices enumerated at the bottom.
**Tracks:** BUILD_ROADMAP §5.5 (M5: Ownership graph + cascading deletion)
— the WM/GFX leg called out by issue **#350**.
**Owner:** kernel/svc/broker_svc + kernel/core/session_manager
**Last reviewed:** 2026-05-26
**Related:** #350 (this slice's tracking issue), #313 (M5 umbrella plan),
#324 / #325 / #326 (M5-SUBSTRATE-002/003/004, slices currently with open PRs
#354 / #344 / #345), #348 / #349 (CAP_GFX_FRAMEBUFFER + CAP_INPUT_*
registration + HAL gate enforcement), #321 / #322 / #328 / #334 / #340
(merged WM / virtual-graphics / PS/2 stack), #241 (`cap_handle_revoke_subtree`
stub reserved), #237 / #240 (cap_handle layer + subject revoke),
#272 (`console_svc`), #98 (audit-event wiring; gates the cascade-audit
SKIP markers), #265 (canonical deny emitter), #221 / #244 (CAP:DENY
conformance + audit byte-fixture).

## Motivation

BUILD_ROADMAP §5.5 requires:

> Validate:
> - deleting Launcher removes owned app modules/resources
> - delegated caps derived from deleted owner become invalid

The M5 ownership-graph plan #313 covers cap_handle-shaped resources via
the broker_svc cascade orchestrator (#324) and its allow / deny QEMU
peers (#325 / #326). Those slices revoke every cap_handle minted under
the owner's broker-port subtree, and `process_destroy` already calls
`cap_handle_revoke_subject` (#240) as belt-and-suspenders cleanup.

What none of those slices touch is the **second class of owned
resource** that landed after #313 was drafted: per-launcher
**window-manager sessions** and the **virtual graphics framebuffer
mappings** carved out of each session record (#321 / #322 / #334 /
#340).

Today, when an M5-managed owner subject is deleted via
`BROKER_OP_DELETE_OWNER`, the cascade walker:

1. Revokes every cap_handle minted under the owner's broker-port
   subtree (#324 slice 1 — flips `cap_handle_revoke_subtree`).
2. Revokes every cap_handle bound to the owner subject id via
   `process_destroy → cap_handle_revoke_subject` (#240).
3. Emits per-edge + summary cascade audit events (SKIP today, gated
   on #98).

But step 2's bulk subject-revoke only affects the cap_handle table —
the session_manager's per-session bookkeeping (`g_sessions[i]` in
`kernel/core/session_manager.c`) is **not** walked. A WM session
opened by the deleted owner stays alive, its VFB / virtual-mouse
state persists, the window-id remains valid from the compositor's
point of view, and any framebuffer-mapping cap_handle the §349 HAL
gates eventually mint stays tied to a session whose owner is gone.

That violates §5.5's first validation bullet: deleting the launcher
**does not** remove the owned window-manager session. It also
violates the README §"Design Principles" zero-trust posture once #349
lands and the gfx/input HAL paths actually check caps, because the
session-id → subject-id binding becomes the load-bearing authority
anchor for those gates.

This plan is the M5 analogue of the §5.5 GFX leg: it extends the
cascade orchestrator to teardown WM sessions owned by the deleted
owner, mirrors the cap-side audit-event shape, and lands two `_qemu`
peers that prove the leg end-to-end on the real M1 substrate.

## Non-goals

- **Replacing the M5 broker cascade.** This plan stacks **on top of**
  the merged #324 / #325 / #326 trio (or whatever subset has landed
  at execute time); it does not re-litigate the broker-side
  orchestrator, the `BROKER_OP_DELETE_OWNER` op-tag, the
  bystander/idempotence/recycle sub-checks, or the deny-marker
  grammar. WM teardown is an additive **step 3.5** between
  `cap_handle_revoke_subtree` (step 3) and `process_destroy` (step 4).
- **A new capability id.** No `CAP_WINDOW_*` or `CAP_SESSION_*` id is
  added. The gfx/input caps from #348 (CAP_GFX_FRAMEBUFFER /
  CAP_INPUT_KEYBOARD / CAP_INPUT_MOUSE) are *already* the gates the
  §349 HAL slice enforces; their revocation falls out of the existing
  cap_handle bulk-revoke without new ABI surface.
- **Reparenting / session transfer to a surviving owner.** Same punt
  as #118 / #313. Sessions of a deleted owner are destroyed, full
  stop.
- **A persistent window-state log across reboot.** Same punt as #313
  for the broker-share ledger.
- **A real compositor "owner gone" UI affordance.** Compositor sees
  the session disappear and the next render pass simply omits it;
  any visible side-effect (e.g. fade-out, "[CLOSED]" banner) is a §6
  follow-up.
- **Touching the marker grammar.** The cascade still reuses the §4
  canonical emitter from #265; no new `CAP:DENY:` shape.
- **An `OS_ABI_VERSION` bump.** Internal-only changes inside
  `broker_svc.c` + `session_manager.c`. `docs/abi/*` is unchanged.
- **Cross-machine sessions, SMP, preemption.** Carried over from
  #313's non-goals list.

## Explicit preconditions

This plan executes once **all** of the following are on `main`:

1. **#324 (M5-SUBSTRATE-002):** `broker_svc_delete_owner` exists with
   its six-step orchestrator. The new WM teardown call slots in as
   **step 3.5** between the existing step 3
   (`cap_handle_revoke_subtree`) and step 4 (`process_destroy`). If
   #324 is still open at execute time, slice 1 below rebases on top
   of it.
2. **#240 (`cap_handle_revoke_subject`):** already on `main`. Used as
   belt-and-suspenders cleanup for any framebuffer-mapping cap_handles
   the §349 HAL slice mints; we do **not** duplicate that walk here.
3. **session_manager exposes a deterministic subject → session-id
   enumerator.** Today `g_sessions[i].subject_id` is set at create
   time (`kernel/core/session_manager.c:187`) and reset to `0u` on
   destroy. The plan's slice 1 adds the tiny enumerator predicate
   `session_manager_first_session_for_subject(cap_subject_id_t,
   unsigned *out_id)` (slot-order scan, returns `0` on hit, `-1` on
   miss). No allocation, no heap, mirrors the M2/M3/M4 substrate
   discipline (see kernel/svc/broker_svc.c side-table).
4. **#348 / #349 are *not* required.** WM session teardown happens
   regardless of whether the gfx/input caps are wired into the HAL
   yet. If #349 has landed, the per-frame HAL deny falls out
   automatically once the session is gone; if it has not, the gfx
   resources still get torn down by virtue of the session row being
   freed. The plan is robust against either order.

The cascade-audit SKIP markers (`audit_wm_cascade_recorded`,
`audit_wm_cascade_done_recorded`) gate on #98, identical-in-shape to
the existing `audit_cascade_recorded` / `audit_cascade_done_recorded`
SKIPs that PR #344 already emits.

## Design surface

### Topology after this plan lands

```
broker_svc_delete_owner(owner, ...):
  step 1.  authority gate (cap_broker_delete_owner_check)        [#324]
  step 2.  per-edge cascade audit emit  (SKIP, gated on #98)     [#324]
  step 3.  cap_handle_revoke_subtree(owner_broker_handle)        [#324]
  step 3a. for each session owned by owner:                      [THIS PLAN]
             - emit cap.revoked.cascade { child_kind="SESSION" }
                 (SKIP, gated on #98)
             - session_manager_destroy(session_id)
  step 4.  process_destroy(owner_pid) → cap_handle_revoke_subject [#324]
  step 5.  summary cap.cascade.done                              [#324]
  step 6.  reply BROKER_SVC_OK + n_children (cap + session count) [#324]
```

The cap-side cascade and the session-side cascade are siblings under
the same orchestrator — one transactional teardown.

### What changes (per surface)

1. **`session_manager`** (`kernel/core/session_manager.{c,h}`):

   - New predicate
     ```c
     int session_manager_first_session_for_subject(cap_subject_id_t subject,
                                                   unsigned int *out_session_id);
     ```
     deterministic slot-order scan of `g_sessions[]`, returns `0`
     and writes `*out_session_id` on hit, `-1` on miss. No
     allocation, no recursion.

   - **No** changes to `session_record_t`. The existing `subject_id`
     field set at `session_manager_create_session` is the load-bearing
     identity anchor.

   - **No** changes to `session_manager_destroy`. The cascade calls
     it the same way the user-driven session-exit path does today.
     Idempotence is already a property of the existing free-slot
     check (the `subject_id == 0u` reset on destroy makes a
     re-enumeration immediately skip the now-free slot).

2. **`broker_svc`** (`kernel/svc/broker_svc.c`):

   - In `broker_svc_delete_owner`, between the existing step 3 and
     step 4, add a bounded loop:
     ```c
     for (int i = 0; i < SESSION_MAX; ++i) {
       unsigned int sid;
       if (session_manager_first_session_for_subject(owner_subject, &sid) != 0) break;
       /* SKIP audit emit (gated on #98): cap.revoked.cascade child_kind=SESSION */
       session_manager_destroy(sid);
       n_session_children += 1;
     }
     ```
     The `SESSION_MAX` outer bound is the existing v0 cap; the inner
     `break` on miss makes the loop terminate after exactly one
     destroy per owned session. No recursion, no heap.

   - `broker_svc_delete_owner`'s `*out_n` total is now
     `n_cap_children + n_session_children` so the reply count surfaces
     both legs to the caller. The existing M5-SUBSTRATE-003/004
     `_qemu` peers (PR #344 / PR #345) assert `n_children > 0` and
     do **not** pin a specific value, so the additive count change
     does not regress them.

   - Header doc-block on `broker_svc.h` adds one line under the
     existing six-step ordering invariant: "step 3.5 — WM session
     teardown via `session_manager_destroy`, after subtree revoke,
     before `process_destroy`, audit SKIP gated on #98".

3. **No changes** to:
   - `kernel/cap/cap_handle.c` — slice 1 of #324 already gives us the
     subtree walker; this plan reuses it unchanged for the cap leg.
   - `docs/abi/*` — no public surface moves.
   - `manifests/schema/v0.json` — no manifest field added; an owner's
     WM-session ownership is implicit in the existing
     `session_manager_create(subject_id, ...)` call site, not declared.
   - `capability-registry.json` — no new cap id.

### Audit events (extends #313 §"Audit emission")

The same two event shapes #313 already documents, with one additive
`child_kind` value:

```
{ "kind": "cap.revoked.cascade",
  "owner": "<owner_subject_id>",
  "child_kind": "SESSION",                  // NEW value; CAP existed in #313
  "child": "<session_id u32>",
  "reason": "owner_deleted",
  "seq": <u64> }
```

The `cap.cascade.done` summary is unchanged; its `n_children` simply
includes WM sessions in the count.

`child_kind` is an additive enum string — same discipline #313 used
for `CAP`. No registry change, no schema bump.

### Capability id reuse (no new id)

Window-manager session teardown does not gate on a new capability.
The authority anchor is `actor_subject_id == owner_subject_id` (or
admin), enforced by `cap_broker_delete_owner_check` exactly the way
the cap leg already does. No addition to `capability_id_t`. ABI-zero.

## ABI surface touched

- **None at the user/kernel boundary.** No additions to
  `docs/abi/syscalls.md`, `docs/abi/capabilities.md`,
  `docs/abi/ipc-wire.md`, `docs/abi/manifest.md`, or
  `docs/abi/capability-registry.json`.
- **Internal-only:**
  - `kernel/core/session_manager.h` gains
    `session_manager_first_session_for_subject`.
  - `kernel/svc/broker_svc.c::broker_svc_delete_owner` gains
    step 3.5.
  - `kernel/svc/broker_svc.h` doc-block adds one ordering bullet.
- **Test surface** (new `_qemu` peer + counter wiring) — see slices below.

## Risks and explicit assumptions

- **Risk:** an owner with no WM session sees a zero-length step 3.5
  loop, and the cascade summary count regresses from "matches cap
  count" to "matches cap + session count".
  *Mitigation:* the existing #325 / #326 `_qemu` peers assert
  `n_children > 0` rather than a specific value (verified by reading
  PR #344's body). The new `_qemu` peer asserts the session-leg
  explicitly via a session-handle gate-check rather than via the
  combined count. Slice 3 author confirms no `n_children == N`
  literal in any merged M5 test before opening.

- **Risk:** `session_manager_first_session_for_subject` could return
  a session whose `subject_id == 0u` if the caller passes `0u` as the
  owner subject.
  *Mitigation:* `0u` is the launcher-bootstrap subject id reserved
  at boot (see `session_manager_start(bootstrap_subject_id)` —
  bootstrap is not the kind of subject that ever calls
  `BROKER_OP_DELETE_OWNER`). The predicate explicitly rejects
  `subject == 0u` and returns `-1` to make the misuse loud rather
  than silently wiping the bootstrap session.

- **Risk:** `session_manager_destroy` is called from a code path
  (`broker_svc_delete_owner`) that runs in the kernel context of
  the *requester* — not the session's owner. The existing
  `session_manager_destroy` code path is documented as "called from
  the session's own exit path"; verify it does not assume `current ==
  session->subject_id`.
  *Mitigation:* the existing implementation is a `g_sessions[]` slot
  rewrite plus `wm_managed = 0` — no `current_subject`-dependent
  state. The test peer asserts that a third-party admin delete
  (`actor == SUBJECT_M5_ADMIN`, not owner) successfully destroys
  the owner's session and the session-record fields are cleared.

- **Risk:** the `_qemu` peer needs a real WM session to destroy,
  which requires the launcher's window-manager session-create path.
  At plan-write time, `session_manager_create` is the host-callable
  surface; the `_qemu` peer reuses it directly the same way #344's
  peer drives `broker_svc_approve_h`.
  *Mitigation:* explicit. No new surface needed.

- **Assumption:** all of #324, #240, #237, #246, #265, and the
  merged WM stack (#321 / #322 / #334 / #340) are on `main` at
  execute time. Verified at plan-write time @
  `git rev-parse main` ≈ `a4dcdd9` (post-PR #340 merge).

## Acceptance demo (maps directly to §5.5 second leg)

| §5.5 validation bullet | Substrate-ridden `_qemu` test | M1 / M5 primitive exercised |
| --- | --- | --- |
| "deleting Launcher removes owned **app modules/resources**" (session leg) | `TEST:PASS:m5_owner_delete_cascade_window_qemu` | `process_create` (#238) + `session_manager_create` (existing) + `BROKER_OP_DELETE_OWNER` (#324) → `session_manager_first_session_for_subject` returns `-1` for the owner subject after the cascade returns |
| "delegated caps derived from deleted owner become invalid" (gfx/input leg) | `TEST:PASS:m5_owner_delete_cascade_window_qemu:delegated_gfx_caps_invalid` | once #349 lands: any cap_handle minted for `CAP_GFX_FRAMEBUFFER` / `CAP_INPUT_*` under the owner's broker-subtree fails `cap_gate_check_handle` with `CAP_ERR_MISSING`. Until #349 lands: assertion is `TEST:SKIP` gated on #349 with marker shape `TEST:SKIP:m5_owner_delete_cascade_window_qemu:delegated_gfx_caps_invalid` (same SKIP discipline as #344's audit SKIPs) |
| (recycle, mirrors #326) | `TEST:PASS:m5_owner_delete_cascade_window_qemu:session_slot_recyclable` | a fresh `session_manager_create(owner_subject)` after the cascade succeeds and lands in a fresh slot; old session-id (now-free) returns "no session" from any read API |
| (idempotence) | `TEST:PASS:m5_owner_delete_cascade_window_qemu:double_delete_idempotent_session_leg` | second `BROKER_OP_DELETE_OWNER` on the same owner returns `BROKER_SVC_OK` with `n_session_children == 0` (the loop's `first_session_for_subject` miss is the no-op) |
| (audit, deferred to #98) | `TEST:SKIP:m5_owner_delete_cascade_window_qemu:audit_wm_cascade_recorded` | gated on #98 / cascade-audit wiring; identical SKIP shape to #344's `audit_cascade_recorded` |
| (audit summary, deferred to #98) | `TEST:SKIP:m5_owner_delete_cascade_window_qemu:audit_wm_cascade_done_recorded` | same gating |

The `_qemu` markers wire into `build/scripts/test.sh` and
`scripts/test.ps1` (the latter forwards by name through the toolchain
docker image — no .ps1 sibling needed, same posture as the other
`m4_*_qemu` and `m5_*_qemu` targets).

## Follow-up implementation issues to file

These execute issues are the concrete units of work this plan
unblocks. Each is sized for one PR / one agent session.

1. **"M5-SUBSTRATE-005a: `session_manager_first_session_for_subject` enumerator (BUILD_ROADMAP §5.5)"**
   - Adds the predicate to `kernel/core/session_manager.{c,h}`.
   - Unit test `session_manager_subject_enumerator_test.c` covering
     hit / miss / `subject == 0u` rejection / post-destroy
     re-enumeration / no false-hit on `subject_id == 0u` free slots.
   - No new ABI surface; not in `TEST_TARGETS` until slice b lands.

2. **"M5-SUBSTRATE-005b: `broker_svc_delete_owner` step 3.5 — WM session teardown (BUILD_ROADMAP §5.5)"**
   - Wires the bounded loop into
     `kernel/svc/broker_svc.c::broker_svc_delete_owner`.
   - Updates the `broker_svc.h` doc-block with the step-3.5 ordering
     bullet.
   - Host-fixture test
     `broker_svc_delete_owner_destroys_owned_sessions_test.c` that
     creates an owner subject, opens a session via
     `session_manager_create`, drives
     `broker_svc_delete_owner(owner, owner, ...)` with
     `PID_INVALID` (PCB teardown suppressed so the session leg is
     observable in isolation), and asserts
     `session_manager_first_session_for_subject(owner, &sid) == -1`
     plus the returned `*out_n` reflects the session in the count.
   - No new ABI surface.

3. **"M5-SUBSTRATE-005c: `m5_owner_delete_cascade_window_qemu` substrate test (BUILD_ROADMAP §5.5)"**
   - Lands `tests/m5_owner_delete_cascade_window_qemu_test.c`
     covering the four PASS sub-checks
     (`m5_owner_delete_cascade_window_qemu`,
     `:session_slot_recyclable`,
     `:double_delete_idempotent_session_leg`) and the two
     `audit_wm_cascade_*` SKIPs plus the `delegated_gfx_caps_invalid`
     SKIP-or-PASS depending on whether #349 is on `main`.
   - Wires into `build/scripts/test.sh` and is added to
     `validate_bundle.sh`'s `TEST_TARGETS` only after #344 / #345
     have landed their `m5_owner_delete_cascade_{allow,deny}_qemu`
     targets there (same merge-order discipline #325 / #326 already
     follow per their PR bodies).

A non-blocking housekeeping follow-up
(**M5-SUBSTRATE-005-DOC**) covers:

- Once #349 lands, upgrade the `delegated_gfx_caps_invalid` SKIP to
  PASS in slice c's `_qemu` peer (one-line marker flip).
- Once #98 cascade-audit wiring lands, upgrade both
  `audit_wm_cascade_*` SKIPs to PASS (identical to the in-flight
  M5-SUBSTRATE-DOC item #313 already tracks for the cap-side
  `audit_cascade_*` markers — recommend folding into the same
  housekeeping PR).

## Out of scope (explicit non-asks)

- Reparenting / session transfer to a surviving owner.
- A persistent window-state log across reboot.
- A compositor "owner gone" UI affordance (fade-out, banner).
- A new `CAP_WINDOW_*` or `CAP_SESSION_*` capability id.
- An `OS_ABI_VERSION` bump.
- `child_kind` enum values beyond `SESSION` (e.g. `MODULE`,
  `FILE`) — those land when their respective resource managers
  acquire the subject-enumerator predicate this plan adds for
  `session_manager`.
- Touching #324 / #325 / #326 directly. WM teardown stacks **on
  top of** them.
- Preemption, SMP, demand paging, ring transitions — carried over
  from #313's non-asks.
- Mirroring into a real QEMU ISO boot — the `_qemu` suffix denotes
  "rides on the real M1 substrate", same precedent as every other
  `m{1,2,3,4,5}_*_qemu` target on `main`.

_Filed by hourly issue-work cron 2026-05-26 — fills the §5.5 GFX/WM
leg that #313 left open, sized to land after the in-flight #324 /
#325 / #326 stack._
