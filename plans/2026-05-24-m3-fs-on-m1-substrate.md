# 2026-05-24 M3 fs_service re-platformed onto merged M1 substrate + M2 console_svc precedent (Plan)

**Status:** Plan-only (per #149 / #192). Implementation deferred to follow-up execute issues enumerated below.
**Tracks:** BUILD_ROADMAP §5.3 (M3: Filesystem service + faux FS).
**Owner:** kernel/fs + launcher
**Last reviewed:** 2026-05-24
**Related:** #259 (M2 substrate plan, merged via #263), #268/#269/#270/#271 (the four M2 substrate slices, all merged), #82/#83 (M3 umbrella), #108 (M3 acceptance tests, merged), #220/#229 (sync IPC v0), #237/#240/#247 (cap_handle layer), #238 (process table), #249 (address_space carve-out), #254 (scheduler block/wake), #246 (handle-gated IPC), #221 (CAP:DENY conformance), #265 (canonical emitter), `plans/2026-04-16-filesystem-service-faux-fs.md` (the original 40-line M3 plan that predates every M1 primitive).

## Motivation

M3 (`BUILD_ROADMAP.md` §5.3) already has its acceptance suite green on
`main` and structurally complete:

| Marker | Test source |
| --- | --- |
| `TEST:PASS:fs_service_persist_allow` (+ 5 sub-checks) | `tests/fs_service_persist_allow_test.c` |
| `TEST:PASS:fs_service_persist_deny` (+ deny-marker assertion) | `tests/fs_service_persist_deny_test.c` |
| `TEST:PASS:fs_service_ephemeral_reset` (+ 4 sub-checks) | `tests/fs_service_ephemeral_reset_test.c` |

But every one of those tests calls `launcher_fs_*` and `fs_*` as flat C
functions inside a single host process — the same pre-substrate shape
M2's `helloapp_*` tests had before #259 → #268/#269/#270/#271
re-platformed them. None of the M3 deny/ephemeral paths ride on:

- the M1 process abstraction (`process_create` / `process_destroy`, #238),
- per-process `address_space_t` windows (#249),
- the cooperative scheduler block/wake plumbing (#254),
- the synchronous IPC v0 primitive (`ipc_send` / `ipc_recv` / `ipc_call`, #220/#229),
- the handle-gated IPC peers (`ipc_send_h` / `ipc_recv_h`, #246),
- the 32-bit capability handle layer (#237/#240/#247),
- the M2 launcher-spawn-from-manifest plumbing (#273),
- the in-kernel service-module precedent set by `console_svc` (#272).

So M3 is technically green on paper while M4 (broker, §5.4) is about to
start spawning provider/consumer pairs through the launcher and minting
`CAP_FS_*` handles per share — and the M3 deny/ephemeral surface those
shares must enforce against has never actually been exercised through
the M1 substrate end-to-end.

This plan is the **M3 analogue of #259**: a plan-only doc that
enumerates the PR-shaped slices needed to re-platform the existing M3
acceptance suite onto the merged M1 substrate, mirroring the
console_svc pattern that #268/#269/#270/#271 already proved works.

## Non-goals

- **Replacing** the existing M3 host-fixture tests. They stay as the
  fast pre-flight tier (`build/scripts/test.sh fs_service_persist_allow`
  etc.). The re-platforming adds *peer* `_qemu` targets that ride on
  the real substrate. Pre-flight markers MUST keep their exact spelling
  — they are the contract observed by `validate_bundle.sh` (#110) and
  the daily capability matrix (#155 / #236). Same discipline as #259.
- New M3 semantics. No new fs storage backend, no new persistence
  modes, no quota/sub-mount/cross-app surface. The only new code is the
  **IPC + cap_handle wiring** that puts the existing `fs_service` and
  `launcher_fs` on the M1 substrate.
- A real on-disk fs_service. The existing ramfs + faux backends are
  what M3 owns; this plan does not extend them.
- ABI change. No `OS_ABI_VERSION` bump; no additions to
  `capability_id_t` (`CAP_FS_READ = 6` and `CAP_FS_WRITE = 7` already
  exist); no new `ipc_result_t` value; no new `fs_result_t` value.
  Same constraint as #259.
- A new `CAP_FS_PERSIST` capability. The existing tests gate on
  `launcher_fs_grant_write` (an internal launcher policy decision),
  not on a separate cap id. This plan keeps that shape.
- Preemption, SMP, demand paging, ring transitions — explicit non-asks
  carried over from #198 / #259.
- Touching the marker grammar in `docs/abi/capability-deny-contract.md`
  (the issue cluster #260/#261 is blocked on that and is **out of
  scope here** — see "Explicit precondition" below).

## Explicit precondition

This plan deliberately does **not** depend on a resolution to the
`#260` / `#261` marker-grammar collision. Every fs deny path already
emits a §4-compliant `CAP:DENY:<subject>:fs_read:<path>` or
`CAP:DENY:<subject>:fs_write:<path>` marker through the canonical
emitter (`cap_deny_marker_format`, post-#265). The substrate slices
below assert against those existing strings unchanged. If the
marker-grammar conversation later widens the contract, the `_qemu`
peers inherit any new shape transparently via the same emitter.

## Design surface

### Topology after re-platforming

```
┌───────────────────────────────────┐    ipc_send_h (CAP_FS_READ/WRITE handle)
│  HelloApp / Editor (process_      │ ────────────────────────────────────┐
│  create, own address_space_t,     │                                     │
│  subject = 3)                     │                                     ▼
└───────────────────────────────────┘                          ┌──────────────────────────┐
                ▲                                              │  fs_svc (in-kernel       │
                │ cap_handle_t minted by launcher              │  module, owns the fs     │
                │ per manifest, passed in HelloApp's           │  port, subject = 2)      │
                │ initial IPC scratch slot                     └──────────────────────────┘
                ▼                                                          │
┌───────────────────────────────────┐                                      ▼
│  Launcher (process_create, own    │                       calls existing launcher_fs_app_*
│  address_space_t, subject = 1)    │                       which routes to fs_service_* with
│  - reads manifest (#226 schema)   │                       persistent / ephemeral demux
│  - calls launcher_fs_register_app │
│    + launcher_fs_grant_*          │
│  - mints CAP_FS_READ/WRITE        │
│    handles per granted op         │
│  - spawns app                     │
└───────────────────────────────────┘
```

All three subjects (launcher, fs_svc, app) become **real M1 processes**.
Every cap_gate decision routes through `cap_gate_check_handle` (#237),
not `cap_check(subject, cap)`. The fs_svc loop is structurally identical
to the M2 console_svc loop (`kernel/svc/console_svc.c`, #272) — one
boot-time IPC port, one bounded-turn-count recv loop, one fan-out into
the existing service implementation.

### What changes (per surface)

1. **fs_svc service module** — new file `kernel/svc/fs_svc.{c,h}`
   (mirrors `kernel/svc/console_svc.{c,h}` exactly in shape):
   - Allocates **two** well-known IPC ports via `ipc_port_alloc` at boot:
     one read port (send_cap = `CAP_FS_READ`), one write port
     (send_cap = `CAP_FS_WRITE`). Owner = fs_svc subject (id 4 reserved;
     console_svc took 2, M2 launcher took 1, HelloApp took 3 per
     `kernel/user/helloapp.c`).
   - Runs as a registered M1 module via
     `process_create(name="fs-svc", entry=fs_svc_loop, ...)`. The loop
     calls `ipc_recv_h` in a bounded turn count, switches on a small
     op enum encoded in `msg.tag` (`FS_SVC_OP_READ=1`, `_WRITE=2`,
     `_LIST=3`, `_UNLINK=4`), and forwards to the existing
     `launcher_fs_app_read` / `_write` etc.
   - Exposes `fs_svc_init()` to be wired into the boot order between
     `ipc_port_table_init` and `proc_init`'s module-registry walk —
     identical edge to the console_svc one added by #272.
   - Reuses the shared `tests/harness/m2_subjects.{c,h}` helpers
     introduced by slice 1 of #259 (no need for a new harness).

2. **Launcher slice** — extend `kernel/user/launcher.{c,h}` and
   `kernel/user/launcher_fs.{c,h}`:
   - New entry point
     `launcher_fs_spawn_app_with_fs_caps(const launcher_manifest *m,
     process_id_t *out_pid)` that:
     1. Calls `process_create` to allocate a PCB + carve an
        `address_space_t` window.
     2. Registers the new process with `launcher_fs_register_app`
        (mode driven by the manifest's `persistence` field — currently
        absent; falls back to `LAUNCHER_FS_MODE_EPHEMERAL` if not set,
        which is the existing default behaviour).
     3. For each fs cap policy approves (`CAP_FS_READ`, `CAP_FS_WRITE`),
        calls `cap_table_grant_as_for_tests` then `cap_handle_mint` to
        produce a `cap_handle_t` bound to the new subject.
     4. Writes minted handles into the new process's IPC scratch slot
        (`address_space_t::ipc_scratch[0..7]` for read handle,
        `[8..15]` for write handle — extends the
        `docs/architecture/m1-m2-handoff.md` convention from #273
        without changing existing bytes 0..7).
     5. Marks the PCB READY and returns its pid.
   - Keep `launcher_fs_app_*` unchanged — they become the in-kernel
     fan-out the fs_svc loop calls into.

3. **HelloApp fs variant** — extend `kernel/user/helloapp.{c,h}`
   (M2 substrate landed via #274):
   - Add an opt-in "fs demo" entry that reads the fs handles out of
     ipc_scratch[0..15], looks up the fs_svc ports via
     `module_registry_find_port("fs-svc-read")` /
     `module_registry_find_port("fs-svc-write")`, and issues a
     `ipc_send_h` round-trip for one read and one write.
   - On `IPC_OK`, emits `TEST:PASS:m3_helloapp_fs_qemu_op` (one per op).

4. **Allow / deny / ephemeral re-platforming** — three new test binaries
   that live alongside the existing host-fixture ones:

   | New test binary | Mirrors host fixture | New marker(s) emitted |
   | --- | --- | --- |
   | `tests/m3_fs_persist_allow_qemu_test.c` | `fs_service_persist_allow_test.c` | `TEST:PASS:m3_fs_persist_allow_qemu`, plus per-sub-check `_qemu` peers for the 5 sub-checks |
   | `tests/m3_fs_persist_deny_qemu_test.c`  | `fs_service_persist_deny_test.c`  | `TEST:PASS:m3_fs_persist_deny_qemu`, `TEST:PASS:m3_fs_persist_deny_marker_qemu` |
   | `tests/m3_fs_ephemeral_reset_qemu_test.c` | `fs_service_ephemeral_reset_test.c` | `TEST:PASS:m3_fs_ephemeral_reset_qemu`, plus the 4 sub-check `_qemu` peers (write_to_faux_succeeds, visible_in_same_session, gone_after_relaunch, no_persist_leak) |

   The `_qemu` suffix matches the M2 convention from
   `m1_ipc_demo_test.c` / `m2_helloapp_*_qemu_test.c`. Each new test:
   - boots an in-kernel scenario via `process_create` (no QEMU image
     actually required for the host-side variant; the suffix denotes
     "rides on the real M1 substrate", same convention #259 already
     committed to and #270/#271 honored),
   - drives the launcher → app → fs_svc round trip,
   - asserts both the structured marker shape **and** the
     `CAP:DENY:<subject>:fs_write:<path>` line on deny paths via the
     #221 conformance validator (`cap_deny_marker_validate`),
   - on the ephemeral relaunch path, calls
     `process_destroy(app_pid)` → `process_create(...)` and asserts the
     previously-written ephemeral data is gone (the existing test
     already calls `launcher_fs_app_relaunch`; the substrate peer adds
     the real process recycle).

5. **Cap revocation on app exit** — same contract as M2's
   `launcher_console_revoke_restores_deny`, repeated for fs. The
   substrate makes it observable: `process_destroy(pid)` already calls
   `cap_handle_revoke_subject(subject)` (#240, verified live at
   `kernel/proc/process.c:174` during #271 pre-audit), so every
   previously-minted fs handle for the dead subject now fails
   `cap_gate_check_handle` with `CAP_GATE_ERR_REVOKED`. The new
   `m3_fs_persist_deny_qemu` proves this by holding the pre-destroy
   handle in its own scope and re-attempting `ipc_send_h` after
   destroy. No new precondition slice needed (mirrors the audit outcome
   recorded in #271's PR description for `cap_handle_revoke_subject`).

### Manifest schema interaction (#226)

The launcher fs slice reads two manifest fields:
- `requested_caps` — already in the schema; the slice consults it for
  `CAP_FS_READ` and `CAP_FS_WRITE` membership (today only `console_write`
  is consumed; this slice is additive).
- `persistence` — **does not exist** in the v0 schema. The slice
  defaults to `LAUNCHER_FS_MODE_EPHEMERAL` when absent (matches
  existing default). Adding a `persistence` enum to the manifest is
  filed as a **non-blocking** follow-up (M3-SUBSTRATE-DOC) and is not
  part of this plan's slice list — schema changes need their own
  versioning review.

### IPC scratch as initial-handoff vector

Extends the convention from #273 / `docs/architecture/m1-m2-handoff.md`:

| Bytes | Meaning | Owner |
| --- | --- | --- |
| `[0..7]` | LE `cap_handle_t` for `console.write` | #273 |
| `[8..15]` | LE `cap_handle_t` for `fs.read` | this plan, slice 2 |
| `[16..23]` | LE `cap_handle_t` for `fs.write` | this plan, slice 2 |
| `[24..]` | available for normal IPC send buffer | unchanged |

Slice 2 lands the doc update at the same path so the convention has a
single canonical home. No ABI surface — `ipc_scratch` is in-kernel-only
in M1/M2/M3.

## ABI surface touched

- **None at the user/kernel boundary.** No additions to `docs/abi/`
  required by this plan; the substrate slices (#220, #237, #246, #249)
  already froze every relevant constant; `CAP_FS_READ` and
  `CAP_FS_WRITE` are already in `capability_id_t` and `cdm_cap_names[]`
  (verified in `kernel/cap/capability.h:15-16` and
  `kernel/cap/cap_deny_marker.c` post-#265).
- **Internal-only:** new `kernel/svc/fs_svc.h` header declares the fs
  service's module registry entry point and the small op enum its loop
  switches on. Not an ABI surface.

## Risks and explicit assumptions

- **Risk:** doubling the M3 test surface (host fixture + `_qemu` peer)
  doubles the maintenance load (same as #259 risk #1).
  *Mitigation:* the `_qemu` peers share most setup with the M2 peers
  landed by #270/#271 (process_create, ipc_port_alloc,
  cap_handle_mint). The shared helper `tests/harness/m2_subjects.{c,h}`
  from #268 is generalised to `tests/harness/svc_subjects.{c,h}` in
  slice 1 (one-line rename + an fs_svc convenience), with no behaviour
  change for the existing M2 tests.
- **Risk:** two ports (read + write) double the port-alloc surface vs.
  M2's single console port.
  *Mitigation:* both port allocations are wired in the same
  `fs_svc_init` call; both are deterministically ordered after
  `ipc_port_table_init`; both fail-loud if `ipc_port_alloc` returns
  exhaustion. The existing `console_svc_port_alloc` validator pattern
  is forked into a 6-line `fs_svc_port_alloc` validator in slice 1.
- **Risk:** the persistence/ephemeral demux currently lives entirely
  inside `launcher_fs.c`; routing it through an IPC hop (fs_svc) may
  reorder operations relative to the existing host-fixture tests.
  *Mitigation:* the `_qemu` peers assert the same sub-check markers in
  the same order; if the hop reorders, the test fails fast. The host
  fixture suite stays untouched as the pre-flight tier.
- **Risk:** `process_destroy` → `cap_handle_revoke_subject` regression
  (the M2 issue 261 audit precondition).
  *Mitigation:* same audit was already done in #271; the call is live
  at `kernel/proc/process.c:174`. No new precondition slice needed.
- **Risk:** the ephemeral-reset test uses `launcher_fs_app_relaunch`
  today, which is a same-subject reset, not a real `process_destroy` →
  `process_create`. The substrate peer changes that to a real recycle.
  *Mitigation:* slice 4 author audits whether `launcher_fs` keys data
  by `cap_subject_id_t` (it does, per `launcher_fs.h:32-45`) or by pid;
  if by subject and the recycled pid carries the same subject, the
  reset is asserted on the registration boundary, not on the pid. Doc
  this in slice 4's commit.
- **Assumption:** all of #220, #229, #237, #240, #246, #247, #249, #254,
  #268, #269, #270, #271, #265 are landed on `main` (all verified at
  plan-write time @ `bee0ffc`).

## Acceptance demo (maps directly to §5.3 validation bullets)

For each existing §5.3 marker, this plan adds a `_qemu` peer that rides
on the real M1 substrate. The pre-flight (host-fixture) markers stay
unchanged.

| §5.3 marker (pre-flight, kept verbatim) | Substrate-ridden peer (new in this plan) | M1 primitive exercised |
| --- | --- | --- |
| `TEST:PASS:fs_service_persist_allow` | `TEST:PASS:m3_fs_persist_allow_qemu` | `process_create` (#238) + `address_space_t` (#249) + `cap_handle_mint` (#247) + `ipc_send_h` (#246) → `fs_svc` recv-loop |
| `TEST:PASS:fs_service_persist_deny` | `TEST:PASS:m3_fs_persist_deny_qemu` | `cap_gate_check_handle` (#237) returns deny; canonical `CAP:DENY:<app>:fs_write:<path>` validated by #221/#265 |
| `TEST:PASS:fs_service_persist_deny:deny_marker` | `TEST:PASS:m3_fs_persist_deny_marker_qemu` | `ipc_send_h` returns `IPC_ERR_CAP_DENIED`; receiver stays blocked (no spurious wake) |
| `TEST:PASS:fs_service_ephemeral_reset:write_to_faux_succeeds` | `TEST:PASS:m3_fs_ephemeral_reset_qemu:write_to_faux_succeeds` | `ipc_send_h(write)` on ephemeral-mode app returns OK |
| `TEST:PASS:fs_service_ephemeral_reset:visible_in_same_session` | `TEST:PASS:m3_fs_ephemeral_reset_qemu:visible_in_same_session` | `ipc_send_h(read)` returns the just-written blob |
| `TEST:PASS:fs_service_ephemeral_reset:gone_after_relaunch` | `TEST:PASS:m3_fs_ephemeral_reset_qemu:gone_after_relaunch` | `process_destroy` → `process_create` recycle + `cap_handle_revoke_subject` (#240) wipes ephemeral state |
| `TEST:PASS:fs_service_ephemeral_reset:no_persist_leak` | `TEST:PASS:m3_fs_ephemeral_reset_qemu:no_persist_leak` | persistent peer's `ipc_send_h(read)` at the same path returns `FS_ERR_NOT_FOUND` |

All `_qemu` markers wire into `build/scripts/test.sh` and
`build/scripts/test.ps1` with `.shell_parity_allowlist` entries per
#156, and surface in the validator JSON report (#110) the same way
M1/M2 substrate markers already do.

## Follow-up implementation issues to file

These execute issues are the concrete units of work this plan unblocks.
Each is intended to be at most one PR / one agent session. Proposed
titles + done-when bullets:

1. **"M3-SUBSTRATE-001: fs_svc in-kernel module + dual IPC port allocation (BUILD_ROADMAP §5.3)"**
   - Lands `kernel/svc/fs_svc.{c,h}`, wires the recv-loop entry into the
     module registry, and the boot-order edge `ipc_port_table_init →
     console_svc_init → fs_svc_init → proc_init`.
   - Validator target: `fs_svc_port_alloc` asserts both well-known ports
     are allocated with the expected send_caps (`CAP_FS_READ`,
     `CAP_FS_WRITE`).
   - Generalises `tests/harness/m2_subjects.{c,h}` →
     `tests/harness/svc_subjects.{c,h}` (one-line rename + an fs_svc
     convenience; the existing M2 peers continue to compile via a
     header alias).

2. **"M3-SUBSTRATE-002: launcher_fs_spawn_app_with_fs_caps + ipc_scratch fs-handle handoff (BUILD_ROADMAP §5.3)"**
   - Extends `kernel/user/launcher.{c,h}` and
     `kernel/user/launcher_fs.{c,h}` with the new entry point.
   - Defines the
     `address_space_t::ipc_scratch[8..15] = LE(read_handle)` and
     `[16..23] = LE(write_handle)` convention; extends
     `docs/architecture/m1-m2-handoff.md` with the M3 rows.
   - Validator target: `launcher_fs_spawn_handoff` asserts the spawned
     PCB reads back both handles byte-exactly.

3. **"M3-SUBSTRATE-003: HelloApp fs-demo variant + persist allow/deny `_qemu` tests (BUILD_ROADMAP §5.3)"**
   - Extends `kernel/user/helloapp.{c,h}` with the fs-demo entry.
   - Lands `tests/m3_fs_persist_allow_qemu_test.c` +
     `tests/m3_fs_persist_deny_qemu_test.c` with markers from the
     table above; wires both into `test.sh` / `test.ps1`.

4. **"M3-SUBSTRATE-004: ephemeral-reset `_qemu` peer covering all four §5.3 reset sub-checks (BUILD_ROADMAP §5.3)"**
   - Lands `tests/m3_fs_ephemeral_reset_qemu_test.c` covering the four
     sub-check `_qemu` peers from the table above.
   - Asserts data wipe across a real `process_destroy` →
     `process_create` recycle, not just `launcher_fs_app_relaunch`.

A separate, non-blocking housekeeping issue (**M3-SUBSTRATE-DOC**)
covers the eventual `manifests/schema/v0.json` extension to add a
`persistence: "persistent" | "ephemeral"` field plus the
`docs/architecture/m3-substrate-ridden-topology.md` update once all
four slices land — out of scope for this plan because manifest schema
changes need their own versioning review.

## Out of scope (explicit non-asks)

- The marker grammar widening conversation (#260 / #261).
- A real on-disk fs backend or any change to ramfs / faux semantics.
- A new `CAP_FS_PERSIST` capability id (existing tests gate on launcher
  policy, not a separate cap).
- M4 broker share/revoke wiring (separate plan thread — that's the next
  domino once this lands).
- Mirroring into a real QEMU ISO boot — the `_qemu` suffix denotes
  "rides on the real M1 substrate", matching the precedent set by
  `m1_ipc_demo_test.c` and the merged M2 substrate slices.

_Filed by hourly issue-work cron 2026-05-24 — fills the gap between
merged M1+M2 substrate and existing M3 acceptance tests._
