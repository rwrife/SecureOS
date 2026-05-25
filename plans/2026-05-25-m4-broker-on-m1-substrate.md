# 2026-05-25 M4 Capability Broker re-platformed onto merged M1 substrate + M2/M3 service-module precedent (Plan)

**Status:** Plan-only (per #149 / #192). Implementation deferred to follow-up execute issues enumerated below.
**Tracks:** BUILD_ROADMAP §5.4 (M4: Capability Broker + share workflow).
**Owner:** kernel/cap + launcher
**Last reviewed:** 2026-05-25
**Related:** #259 / #263 (M2-on-M1 plan), #276 / #277 (M3-on-M1 plan), #115 (M4 broker acceptance tests, merged), #85 / #99 (broker core), #82/#83 (launcher manifest schema), #220 / #229 / #246 (sync IPC v0 + handle-gated peers), #237 / #240 / #247 (cap_handle layer), #238 (process table), #249 (address_space carve-out), #254 (scheduler block/wake), #265 (canonical deny-marker emitter), #221 / #244 (CAP:DENY conformance + audit byte-fixture), #272 (console_svc precedent), #282 (fs_svc precedent), #287 (boot-order wiring), #273 / #289 (ipc_scratch handle handoff convention), #286 (manifest persistence enum precedent for additive schema fields).

## Motivation

BUILD_ROADMAP §5.4 ("M4: Capability Broker + share workflow") has its
three deterministic acceptance markers landed on `main` and green
on CI:

| Marker | Test source |
| --- | --- |
| `TEST:PASS:broker_share_allow` (+ 5 sub-checks) | `tests/broker_share_allow_test.c` |
| `TEST:PASS:broker_share_deny`  (+ 6 sub-checks, incl. `bystander_cannot_mutate`, `cannot_be_re_approved`) | `tests/broker_share_deny_test.c` |
| `TEST:PASS:broker_share_revoke` (+ 5 sub-checks, owner-revoke and recipient self-revoke) | `tests/broker_share_revoke_test.c` |

But — exactly as #259 found for M2 and #276 found for M3 — every one
of those tests drives `cap_broker_*` and `cap_table_*` as flat C
functions inside a single host process with hand-picked subject ids.
None of the M4 allow / deny / revoke paths ride on:

- the M1 process abstraction (`process_create` / `process_destroy`, #238),
- per-process `address_space_t` windows carved out of `.proc_arena` (#249),
- the cooperative scheduler block/wake plumbing (#254),
- the synchronous IPC v0 primitive (`ipc_send` / `ipc_recv` / `ipc_call`, #220 / #229),
- the handle-gated IPC peers (`ipc_send_h` / `ipc_recv_h`, #246),
- the 32-bit capability handle layer (#237 / #240 / #247),
- the M2 launcher-spawn-from-manifest plumbing (#273 / #289),
- the in-kernel service-module precedent set by `console_svc` (#272) and `fs_svc` (#282 / #287).

So M4 is technically green on paper while M5 (ownership graph +
cascading deletion, §5.5) is about to start needing **delegated
capability handles** to behave correctly under `process_destroy` →
`cap_handle_revoke_subject` recycling. The share request / approve /
revoke surface those M5 cascades will exercise has never actually
been driven through the M1 substrate end-to-end.

This plan is the **M4 analogue of #259 (M2) and #276 (M3)**: a
plan-only doc that enumerates the PR-shaped slices needed to
re-platform the existing M4 acceptance suite onto the merged M1
substrate, mirroring the `console_svc` / `fs_svc` pattern that
#268/#269/#270/#271 and #278/#279/#280/#281 already proved works.

## Non-goals

- **Replacing** the existing M4 host-fixture tests. They stay as the
  fast pre-flight tier (`build/scripts/test.sh broker_share_allow`
  etc.). The re-platforming adds *peer* `_qemu` targets that ride on
  the real substrate. Pre-flight markers (and all sixteen
  `TEST:PASS:broker_share_{allow,deny,revoke}:<sub>` sub-checks) MUST
  keep their exact spelling — they are the contract observed by
  `validate_bundle.sh` (#110) and the daily capability matrix
  (#155 / #236). Same discipline as #259 / #276.
- New broker semantics. No expiration, no time-bounded shares, no
  cross-machine, no multi-recipient — those are explicit non-asks
  carried over from `plans/2026-05-14-m4-broker-acceptance-tests.md`
  and #115. The only new code is the **IPC + cap_handle wiring** that
  puts the existing `cap_broker` and `launcher` on the M1 substrate.
- A new broker capability id. Approval / deny / revoke are gated on
  the existing `cap_broker_*` subject-id checks
  (`approver_subject_id == owner_subject_id`,
  `actor_subject_id ∈ {owner, recipient}`), not on a separate
  `CAP_BROKER_APPROVE` cap. Adding one is a future ABI change and
  out of scope here.
- ABI change. No `OS_ABI_VERSION` bump; no additions to
  `capability_id_t`; no new `ipc_result_t` value; no new
  `cap_broker_result_t` value. Same constraint as #259 / #276.
- A real "prompt" UI for owner approval. The launcher's mediation
  point in v0 is a synchronous, deterministic policy callback —
  exactly the shape the existing tests already exercise. Interactive
  prompts (BUILD_ROADMAP §5.4 "prompt policy integration") are
  deliberately deferred to a follow-up that can rest on the §6
  console_svc TTY surface once it exists.
- Touching the marker grammar in `docs/abi/capability-deny-contract.md`
  (the issue cluster #260/#261 was the canonical home for that; both
  are now closed, and this plan reuses the §4-compliant emitter
  unchanged — same precondition discipline #276 used).
- Preemption, SMP, demand paging, ring transitions — explicit non-asks
  carried over from #198 / #259 / #276.

## Explicit precondition

This plan deliberately does **not** depend on landing any new audit
wiring. The existing broker tests already emit `TEST:SKIP:*:audit_*`
markers for the audit-deny / audit-grant assertions gated behind
#98; the `_qemu` peers below keep that exact skip until #98 lands,
mirroring the host-fixture contract one-for-one.

Every broker deny path that does emit a marker already goes through
the canonical `cap_deny_marker_format` emitter (post-#265, conformance
gated by #221 / #244). The substrate slices below assert against
those existing strings unchanged. If the marker-grammar conversation
later widens the contract, the `_qemu` peers inherit any new shape
transparently via the same emitter.

## Design surface

### Topology after re-platforming

```
┌───────────────────────────────────┐    ipc_send_h (CAP_BROKER_REQ handle, see "Capability id reuse")
│  Owner App (process_create, own   │ ────────────────────────────────────┐
│  address_space_t, subject = 3)    │                                     │
│  - already holds CAP_FS_READ via  │                                     │
│    launcher_fs_grant_read (M3)    │                                     │
└───────────────────────────────────┘                                     │
                                                                          ▼
                                                              ┌──────────────────────────┐
                                                              │  broker_svc (in-kernel   │
                                                              │  module, owns the broker │
                                                              │  port, subject = 5)      │
                                                              │  - drives cap_broker_*   │
                                                              │  - on approve, mints     │
                                                              │    cap_handle_t bound to │
                                                              │    recipient subject     │
                                                              └──────────────────────────┘
                                                                          │
                ┌─────────────────────────────────────────────────────────┘
                │ ipc_send_h reply containing minted recipient handle
                ▼
┌───────────────────────────────────┐
│  Recipient App (process_create,   │
│  own address_space_t, subject = 4)│
│  - reads minted handle out of     │
│    ipc_scratch[24..31] (see       │
│    "IPC scratch as handoff vector"│
│    below)                         │
│  - uses ipc_send_h against fs_svc │
│    port to exercise the shared    │
│    cap                            │
└───────────────────────────────────┘
                ▲
                │ owner-issued revoke is delivered the same way:
                │ a one-shot `ipc_send_h(BROKER_OP_REVOKE)` from the
                │ owner causes broker_svc to call cap_broker_revoke,
                │ which in turn calls cap_handle_revoke for the
                │ minted handle — recipient's next `ipc_send_h`
                │ against fs_svc returns `IPC_ERR_CAP_DENIED`.
                ▼
┌───────────────────────────────────┐
│  Launcher (process_create, own    │
│  address_space_t, subject = 1)    │
│  - reads manifest (#226 schema)   │
│  - on owner spawn, mints          │
│    broker-port send handle into   │
│    ipc_scratch[24..31] (per       │
│    §"Capability id for the        │
│    broker-svc port" below)        │
│  - on recipient spawn, mints      │
│    broker-port send handle into   │
│    ipc_scratch[24..31] (same      │
│    convention)                    │
└───────────────────────────────────┘
```

All subjects (launcher, console-svc, fs-svc, owner, recipient,
broker-svc) become **real M1 processes** with their own PCB,
address_space_t window, and per-process IPC scratch buffer. Every
broker decision routes through `cap_gate_check_handle` (#237) on the
ingress side, and the broker's approve path mints a fresh
`cap_handle_t` for the recipient via `cap_handle_grant` (#247)
rather than just writing the recipient's row in `cap_table` via the
existing `cap_broker_approve` side-effect.

### What changes (per surface)

1. **broker_svc service module** — new file
   `kernel/svc/broker_svc.{c,h}` (mirrors `kernel/svc/console_svc.{c,h}`
   from #272 and `kernel/svc/fs_svc.{c,h}` from #282 exactly in shape):
   - Allocates **one** well-known IPC port via `ipc_port_alloc` at boot
     (send_cap = `CAP_BROKER_REQ`, see "Capability id reuse" below).
     Owner = broker_svc subject (id 5 reserved; launcher took 1,
     console-svc 2, helloapp 3, fs-svc 4).
   - Runs as a registered M1 module via
     `process_create(name="broker-svc", entry=broker_svc_loop, ...)`.
     The loop calls `ipc_recv_h` in a bounded turn count, switches on
     a small op enum encoded in `msg.tag`
     (`BROKER_OP_REQUEST=1`, `_APPROVE=2`, `_DENY=3`, `_REVOKE=4`),
     and fans out into the existing `cap_broker_request_share` /
     `_approve` / `_deny` / `_revoke`.
   - On `BROKER_OP_APPROVE`, after `cap_broker_approve` returns
     `CAP_BROKER_OK`, calls `cap_handle_grant(recipient, cap_id)` and
     writes the minted `cap_handle_t` into the *reply* message body
     (LE-encoded in the first 4 bytes; the recipient app's loop reads
     it back via `ipc_recv_h`). This is the M1 analogue of the
     existing "broker writes to cap_table" side-effect.
   - On `BROKER_OP_REVOKE`, after `cap_broker_revoke` returns OK,
     calls `cap_handle_revoke(handle)` to invalidate the previously
     minted handle. The handle is identified by the (recipient, cap)
     pair carried in the request body — broker_svc keeps a small
     parallel table `share_id -> cap_handle_t` (capped at
     `CAP_BROKER_MAX_SHARES = 8`, same bound as `cap_broker`).
   - Exposes `broker_svc_init()` to be wired into the boot order
     between `fs_svc_init` and `proc_init`'s module-registry walk —
     same edge the console_svc / fs_svc inits sit on (per #287).
   - Reuses the shared `tests/harness/svc_subjects.{c,h}` helper
     introduced alongside `tests/harness/m2_subjects.{c,h}` for the
     fs-svc slice (#282 — the two harnesses coexist; the M2 peers
     keep using `m2_subjects.h`, M3 and later use `svc_subjects.h`).
     One-line addition of a `BROKER_SVC_SUBJECT_ID = 5` convenience
     constant in `svc_subjects.h`.

2. **Launcher slice** — extend `kernel/user/launcher.{c,h}`:
   - Extend `launcher_spawn_app_from_manifest` so that whenever the
     manifest's `requested_caps` includes `CAP_BROKER_REQ`, the
     launcher mints a `cap_handle_t` and writes it into the spawned
     PCB's `ipc_scratch[24..31]` (extending the convention #273 / #289
     established for bytes `[0..7]` console, `[8..15]` fs.read,
     `[16..23]` fs.write). One additive convention update; no
     existing byte ranges move.
   - `docs/architecture/m1-m2-handoff.md` gets a new row for
     `[24..31] = LE(cap_handle_t for cap_broker_req)`.
   - No new launcher entry point. The owner-side and recipient-side
     manifests both declare `CAP_BROKER_REQ` in `requested_caps`; the
     launcher treats the two identically. Approver-vs-recipient
     authority is enforced by `cap_broker_*` inside broker_svc using
     the source subject id `ipc_recv_h` reports, exactly as the
     in-process tests already assert.

3. **Owner / recipient apps** — extend `kernel/user/helloapp.{c,h}`
   (or land sibling app modules `kernel/user/brokerdemo_owner.{c,h}`
   and `brokerdemo_recipient.{c,h}`; final choice deferred to slice
   author — the previous M2/M3 slices put the demo inside `helloapp`
   to avoid module-table churn, and that is the recommended default
   here too).
   - Owner entry: read the `cap_broker_req` handle from
     `ipc_scratch[24..31]`, look up the broker port via
     `module_registry_find_port("broker-svc")`, issue
     `ipc_send_h(BROKER_OP_REQUEST, {recipient, cap, resource})` then
     `ipc_send_h(BROKER_OP_APPROVE, {share_id})`. On reply, emits
     `TEST:PASS:m4_broker_owner_qemu:request`,
     `:approve`.
   - Recipient entry: blocks in `ipc_recv_h` on its own port (allocated
     by the M3 fs-svc handoff pattern) waiting for the minted
     `cap_handle_t` payload from broker_svc's approve reply. On
     receipt, calls `ipc_send_h` against the M3 fs-svc port using the
     minted handle as proof-of-share and emits
     `TEST:PASS:m4_broker_recipient_qemu:read_via_shared_cap`.
   - Revoke entry (owner): issues
     `ipc_send_h(BROKER_OP_REVOKE, {share_id})`; recipient's next
     `ipc_send_h(fs_svc)` returns `IPC_ERR_CAP_DENIED` and emits
     `TEST:PASS:m4_broker_recipient_qemu:read_after_revoke_denied`.

4. **Allow / deny / revoke re-platforming** — three new test binaries
   alongside the existing host-fixture ones:

   | New test binary | Mirrors host fixture | New marker(s) emitted |
   | --- | --- | --- |
   | `tests/m4_broker_share_allow_qemu_test.c`  | `broker_share_allow_test.c`  | `TEST:PASS:m4_broker_share_allow_qemu` plus `_qemu` peers for all 5 sub-checks (`owner_holds_cap`, `request_returns_pending_share_id`, `approve_grants_recipient`, `scope_is_capability_bound`, `scope_is_resource_bound`) |
   | `tests/m4_broker_share_deny_qemu_test.c`   | `broker_share_deny_test.c`   | `TEST:PASS:m4_broker_share_deny_qemu` plus `_qemu` peers for all 6 sub-checks (incl. `bystander_cannot_mutate`, `cannot_be_re_approved`) and the audit-deny `TEST:SKIP:*:audit_deny_recorded_qemu` |
   | `tests/m4_broker_share_revoke_qemu_test.c` | `broker_share_revoke_test.c` | `TEST:PASS:m4_broker_share_revoke_qemu` plus `_qemu` peers for all 5 sub-checks (incl. `owner_revoke_takes_effect`, `recipient_self_revoke`, `underlying_table_revoked`, `double_revoke_is_idempotent`) |

   The `_qemu` suffix matches the M2 / M3 convention from
   `m1_ipc_demo_test.c` / `m2_helloapp_*_qemu_test.c` /
   `m3_fs_*_qemu_test.c`. Each new test:
   - boots an in-kernel scenario via `process_create` (no QEMU image
     actually required for the host-side variant; the suffix denotes
     "rides on the real M1 substrate"),
   - drives owner → broker_svc → recipient → fs_svc round trip,
   - asserts the same observable sub-check outcomes as the existing
     host-fixture tests **plus** that `cap_handle_revoke_subject` on
     either owner or recipient subject correctly invalidates any
     minted handles (M1-substrate-only assertion).

5. **Cap revocation on app exit** — same contract as M2's
   `launcher_console_revoke_restores_deny` and M3's
   `m3_fs_persist_deny_qemu`, extended for broker. The substrate
   makes it observable: `process_destroy(pid)` already calls
   `cap_handle_revoke_subject(subject)` (#240, verified live at
   `kernel/proc/process.c:174`), so every previously-minted broker
   share handle for the dead subject now fails
   `cap_gate_check_handle` with `CAP_GATE_ERR_REVOKED`. The
   `m4_broker_share_revoke_qemu` test proves this end-to-end by
   `process_destroy`-ing the recipient and asserting any prior
   minted handle now denies. No new precondition slice needed
   (mirrors the audit outcome recorded in #271 and #281).

### Capability id for the broker-svc port

The broker-svc port needs a send cap. There is **no**
`CAP_BROKER_REQ` in `capability_id_t` today (verified at plan-write
time — the enum runs `CAP_CONSOLE_WRITE = 1 .. CAP_SYSCALL = 15`,
see `kernel/cap/capability.h:9-37`). Two equally valid options for
the execute slice, both ABI-zero-risk:

1. **Reuse `CAP_IPC_SEND` (recommended).** All M1 substrate apps
   already hold `CAP_IPC_SEND` in order to talk to any service port;
   the broker's *authority* check (approver-vs-owner,
   actor-in-{owner,recipient}) happens inside `cap_broker_*` against
   the source `cap_subject_id_t` that `ipc_recv_h` reports, not at
   the port gate. This is the same shape M3 used: `fs_svc`'s ports
   are gated by `CAP_FS_READ` / `CAP_FS_WRITE` because the read/write
   distinction *is* the capability; for broker, the
   request/approve/deny/revoke distinction is intentionally **not**
   a capability — it's a subject-authority decision. Reusing
   `CAP_IPC_SEND` keeps that property explicit and adds zero ABI
   surface. Same discipline as M3 reusing existing caps without
   inventing new ones.
2. **Additively introduce `CAP_BROKER_REQ = 16`.** Strictly additive
   under `OS_ABI_VERSION = 0` (the slot is free; #232's `CAP_SYSCALL
   = 15` was the last entry). Requires a `docs/abi/capabilities.md`
   stamp bump and a `capability-registry.json` entry per #234. If a
   reviewer prefers the extra grep-able cap id, the execute slice
   does that addition in slice 1 and stamps the ABI doc in the same
   PR. If not, slice 1 uses option 1 and this cap id stays unfiled.

The plan recommends **option 1** as the default — it matches the
intent ("broker authority is subject-bound, not cap-bound") and
minimises ABI churn. Slice 1's PR author makes the final call.

If audit later wants to gate approve / deny / revoke distinctly,
that's an additive change handled in a follow-up — same discipline
as #286's additive `persistence` enum on the manifest schema.

### Manifest schema interaction (#226)

The slice reads one manifest field:
- `requested_caps` — already in the schema; the slice consults it for
  `CAP_BROKER_REQ` membership. Today `console_write`, `fs.read`, and
  `fs.write` are consumed (per #273 / #289); this slice is additive.

No new manifest field is needed. A follow-up to declare a
`broker_role: "owner" | "recipient"` enum (so the launcher could
refuse to mint the handle on the wrong side) is filed as a
**non-blocking** follow-up (M4-SUBSTRATE-DOC) and is not part of
this plan's slice list — schema changes need their own versioning
review (same discipline as M3-SUBSTRATE-DOC → #285 / #286).

### IPC scratch as initial-handoff vector

Extends the convention from #273 / #289 /
`docs/architecture/m1-m2-handoff.md`:

| Bytes | Meaning | Owner |
| --- | --- | --- |
| `[0..7]`   | LE `cap_handle_t` for `console.write`             | #273 |
| `[8..15]`  | LE `cap_handle_t` for `fs.read`                   | #289 |
| `[16..23]` | LE `cap_handle_t` for `fs.write`                  | #289 |
| `[24..31]` | LE `cap_handle_t` for broker-svc send (per §above) | **this plan, slice 2** |
| `[32..]`   | available for normal IPC send buffer              | unchanged |

Slice 2 lands the doc update at the same path so the convention has
a single canonical home. No ABI surface — `ipc_scratch` is
in-kernel-only in M1/M2/M3/M4.

## ABI surface touched

- **None at the user/kernel boundary by default.** No additions to
  `docs/abi/` required by this plan when the slice author picks
  option 1 (reuse `CAP_IPC_SEND`) per §"Capability id for the
  broker-svc port". The substrate slices (#220, #237, #246, #249)
  already froze every relevant constant.
- If the slice author picks option 2 (additively add
  `CAP_BROKER_REQ = 16`), the same PR stamps
  `docs/abi/capabilities.md` per the §7 freeze convention and adds
  the corresponding `capability-registry.json` row per #234. That is
  the only ABI-visible delta this plan ever introduces, and it stays
  additive under `OS_ABI_VERSION = 0`.
- **Internal-only:** new `kernel/svc/broker_svc.h` header declares
  the broker service's module registry entry point and the small op
  enum its loop switches on. Not an ABI surface.

## Risks and explicit assumptions

- **Risk:** doubling the M4 test surface (host fixture + `_qemu`
  peer) doubles the maintenance load (same as #259 risk #1).
  *Mitigation:* the `_qemu` peers share most setup with the M2 / M3
  peers landed by #270/#271/#280/#281 (process_create,
  ipc_port_alloc, cap_handle_grant). The shared helper
  `tests/harness/svc_subjects.{c,h}` from #282 needs only the
  `BROKER_SVC_SUBJECT_ID = 5` convenience addition, no rename.
- **Risk:** broker_svc needs to keep a `share_id -> cap_handle_t`
  side-table to make revoke work, and that table can diverge from
  the existing `cap_broker` internal `share_id -> (owner,
  recipient, cap, resource)` table.
  *Mitigation:* the side-table is bounded to `CAP_BROKER_MAX_SHARES
  = 8` (same constant), cleared by a new `broker_svc_reset()` that is
  always called alongside `cap_broker_reset()` in tests; a dedicated
  test asserts the two tables stay in sync across approve / revoke
  cycles.
- **Risk:** `process_destroy` → `cap_handle_revoke_subject` regression
  (the M2 #261 / M3 #281 audit precondition).
  *Mitigation:* same audit was already done in #271 and #281; the
  call is live at `kernel/proc/process.c:174`. No new precondition
  slice needed.
- **Risk:** broker_svc's loop and fs_svc's loop both running in the
  same scheduler tick budget could deadlock if a recipient's
  `ipc_send_h(fs_svc)` is processed before the broker's reply has
  been consumed.
  *Mitigation:* the cooperative scheduler (#254) drains per-port
  ready queues in port-id order; recipient blocks on
  `ipc_recv_h(own_port)` waiting for the broker reply, so fs_svc
  doesn't get exercised until the broker reply has been delivered
  and the recipient unblocks. Slice 4 author asserts this ordering
  explicitly with a structured `:order_observed_qemu` sub-check.
- **Risk:** the recipient app needs to know its own well-known IPC
  port id so the broker can reply back to it. Today this is implicit
  in the M3 fs-svc flow (the launcher mints a handle bound to the
  fs-svc port, not to the recipient).
  *Mitigation:* the broker reply is delivered on broker_svc's own
  port using a per-call `reply_to_port` id encoded in the request
  body, the same shape `ipc_call` uses today. The recipient
  allocates its own port via `ipc_port_alloc` in its startup and
  writes the port id into the request before sending it. No new
  primitive needed.
- **Assumption:** all of #220, #229, #237, #240, #246, #247, #249,
  #254, #265, #268..#275 (M2 substrate), #278..#282, #287..#291
  (M3 substrate), and the existing broker landings (#99, #115) are
  on `main` at execute time. All verified at plan-write time
  @ `7f303d7`.

## Acceptance demo (maps directly to §5.4 validation bullets)

For each existing §5.4 marker, this plan adds a `_qemu` peer that
rides on the real M1 substrate. The pre-flight (host-fixture)
markers stay unchanged.

| §5.4 marker (pre-flight, kept verbatim) | Substrate-ridden peer (new in this plan) | M1 primitive exercised |
| --- | --- | --- |
| `TEST:PASS:broker_share_allow` | `TEST:PASS:m4_broker_share_allow_qemu` | `process_create` (#238) + `address_space_t` (#249) + `cap_handle_grant` (#247) + `ipc_send_h` (#246) → `broker_svc` recv-loop → reply with minted handle |
| `TEST:PASS:broker_share_allow:approve_grants_recipient` | `TEST:PASS:m4_broker_share_allow_qemu:approve_grants_recipient` | recipient's `ipc_send_h(fs_svc, ..., minted_handle)` returns `IPC_OK`; pre-approve attempt with the same payload returns `IPC_ERR_CAP_DENIED` |
| `TEST:PASS:broker_share_allow:scope_is_capability_bound` | `TEST:PASS:m4_broker_share_allow_qemu:scope_is_capability_bound` | recipient tries `ipc_send_h(fs_svc_write, ..., minted_handle_for_read)` ⇒ `IPC_ERR_CAP_DENIED` via `cap_gate_check_handle` (#237) |
| `TEST:PASS:broker_share_allow:scope_is_resource_bound` | `TEST:PASS:m4_broker_share_allow_qemu:scope_is_resource_bound` | recipient tries the read against a different path ⇒ deny at fs_svc; canonical `CAP:DENY:<recipient>:fs_read:<path>` validated by #221/#265 |
| `TEST:PASS:broker_share_deny` | `TEST:PASS:m4_broker_share_deny_qemu` | broker_svc's `BROKER_OP_DENY` path; no recipient handle ever minted; recipient's `ipc_send_h(fs_svc)` returns `IPC_ERR_CAP_DENIED` |
| `TEST:PASS:broker_share_deny:bystander_cannot_mutate` | `TEST:PASS:m4_broker_share_deny_qemu:bystander_cannot_mutate` | a third process attempting `ipc_send_h(broker_svc, BROKER_OP_APPROVE, ...)` is rejected by `cap_broker_approve`'s `approver != owner` check; broker_svc emits canonical `CAP:DENY:<bystander>:cap_broker_approve:<share_id>` |
| `TEST:PASS:broker_share_deny:cannot_be_re_approved` | `TEST:PASS:m4_broker_share_deny_qemu:cannot_be_re_approved` | after `BROKER_OP_DENY`, owner's `BROKER_OP_APPROVE` for the same share_id returns `CAP_BROKER_ERR_BAD_STATE` |
| `TEST:PASS:broker_share_revoke` | `TEST:PASS:m4_broker_share_revoke_qemu` | owner-initiated revoke invalidates minted handle; recipient's next `ipc_send_h(fs_svc)` returns `IPC_ERR_CAP_DENIED` |
| `TEST:PASS:broker_share_revoke:underlying_table_revoked` | `TEST:PASS:m4_broker_share_revoke_qemu:underlying_table_revoked` | both `cap_table_check` (legacy) and `cap_gate_check_handle` (substrate) return MISSING / REVOKED for the recipient subject after revoke |
| `TEST:PASS:broker_share_revoke:recipient_self_revoke` | `TEST:PASS:m4_broker_share_revoke_qemu:recipient_self_revoke` | recipient calls `BROKER_OP_REVOKE` on its own share_id; cap_broker accepts (owner or recipient); minted handle is invalidated |
| `TEST:PASS:broker_share_revoke:double_revoke_is_idempotent` | `TEST:PASS:m4_broker_share_revoke_qemu:double_revoke_is_idempotent` | second `BROKER_OP_REVOKE` returns `CAP_BROKER_OK` (no-op), minted handle still denies |
| `TEST:SKIP:broker_share_deny:audit_deny_recorded` | `TEST:SKIP:m4_broker_share_deny_qemu:audit_deny_recorded_qemu` | gated on #98 (broker→audit wiring), identical to host fixture |

All `_qemu` markers wire into `build/scripts/test.sh` and
`build/scripts/test.ps1` with `.shell_parity_allowlist` entries per
#156, and surface in the validator JSON report (#110) the same way
M1/M2/M3 substrate markers already do.

## Follow-up implementation issues to file

These execute issues are the concrete units of work this plan
unblocks. Each is intended to be at most one PR / one agent session.
Proposed titles + done-when bullets:

1. **"M4-SUBSTRATE-001: broker_svc in-kernel module + IPC port allocation (BUILD_ROADMAP §5.4)"**
   - Lands `kernel/svc/broker_svc.{c,h}` (loop, op enum, side-table
     for `share_id → cap_handle_t`).
   - Wires the recv-loop entry into the module registry; extends
     the boot-order edge from #287 to
     `ipc_port_table_init → console_svc_init → fs_svc_init →
     broker_svc_init → proc_init`.
   - Validator target: `broker_svc_port_alloc` asserts the
     well-known port is allocated with the chosen send_cap (either
     `CAP_IPC_SEND` per option 1, or `CAP_BROKER_REQ` per option 2 —
     see §"Capability id for the broker-svc port").
   - Extends `tests/harness/svc_subjects.{c,h}` with
     `BROKER_SVC_SUBJECT_ID = 5` (one-line addition; existing M2/M3
     peers continue to compile unchanged).

2. **"M4-SUBSTRATE-002: launcher cap_broker_req handle handoff via ipc_scratch[24..31] (BUILD_ROADMAP §5.4)"**
   - Extends `kernel/user/launcher.{c,h}` to mint a `cap_handle_t`
     for the chosen broker-port send cap (per §"Capability id for
     the broker-svc port") whenever the manifest's `requested_caps`
     marks the app as a broker participant, written LE into
     `ipc_scratch[24..31]`.
   - Extends `docs/architecture/m1-m2-handoff.md` with the M4 row.
   - Validator target: `launcher_broker_spawn_handoff` asserts the
     spawned PCB reads back the handle byte-exactly.

3. **"M4-SUBSTRATE-003: broker_svc allow + deny `_qemu` tests (BUILD_ROADMAP §5.4)"**
   - Extends `kernel/user/helloapp.{c,h}` (or new
     `brokerdemo_{owner,recipient}.{c,h}`) with the owner / recipient
     entries described in slice 3 above.
   - Lands `tests/m4_broker_share_allow_qemu_test.c` +
     `tests/m4_broker_share_deny_qemu_test.c` with markers from the
     table above; wires both into `test.sh` / `test.ps1` (+
     `.shell_parity_allowlist`).
   - The audit-deny `TEST:SKIP` marker is emitted from the deny
     test, identical spelling-modulo-`_qemu`-suffix to the host
     fixture; do not implement audit wiring here (gated on #98).

4. **"M4-SUBSTRATE-004: broker_svc revoke `_qemu` peer covering all five §5.4 revoke sub-checks (BUILD_ROADMAP §5.4)"**
   - Lands `tests/m4_broker_share_revoke_qemu_test.c` covering the
     five sub-check `_qemu` peers from the table above
     (`owner_revoke_takes_effect`, `recipient_self_revoke`,
     `underlying_table_revoked`, `double_revoke_is_idempotent`,
     `setup_grants_recipient`).
   - Asserts substrate-only `process_destroy`-recycle revoke:
     destroying the recipient subject revokes any minted handle via
     `cap_handle_revoke_subject` (#240), and a fresh
     `process_create` for the same subject id starts deny-by-default.

A separate, non-blocking housekeeping issue
(**M4-SUBSTRATE-DOC**) covers the eventual
`manifests/schema/v0.json` extension to add a
`capabilities.broker_role: "owner" | "recipient"` enum plus the
`docs/architecture/m4-substrate-ridden-topology.md` update once all
four slices land — out of scope for this plan because manifest
schema changes need their own versioning review (same discipline
as #285 / #286).

## Out of scope (explicit non-asks)

- The marker grammar widening conversation (#260 / #261 are both
  resolved-closed; this plan reuses the existing canonical emitter).
- A real interactive owner-approval prompt UI (deferred until §6
  TTY console_svc surface lands).
- A new `CAP_BROKER_APPROVE` capability id (existing tests gate on
  subject-id authority, not a separate cap).
- M5 ownership-graph cascading deletion wiring (separate plan
  thread — that's the next domino once this lands).
- `audit_*_recorded` markers being upgraded from SKIP to PASS — that
  is the existing #98 work and is unchanged by this plan.
- Mirroring into a real QEMU ISO boot — the `_qemu` suffix denotes
  "rides on the real M1 substrate", matching the precedent set by
  `m1_ipc_demo_test.c` and the merged M2 / M3 substrate slices.

_Filed by hourly issue-work cron 2026-05-25 — fills the gap between
merged M1+M2+M3 substrate and existing M4 acceptance tests, completing
the substrate-re-platforming trilogy started by #259 / #276._
