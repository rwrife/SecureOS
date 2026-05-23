# 2026-05-23 M2 Console + Launcher + HelloApp re-platformed onto merged M1 substrate (Plan)

**Status:** Plan-only (per #192 / #149). Implementation deferred to follow-up execute issues enumerated below.
**Tracks:** BUILD_ROADMAP §5.2 (M2: Console service + Launcher + HelloApp — first capability slice).
**Owner:** kernel + launcher
**Last reviewed:** 2026-05-23
**Related:** #259 (this plan's tracking issue), #82 (M2 umbrella), #256 (M2 wiring), plan #197 / #233 (cap table + handle layer), plan #198 / #248 (process abstraction + address_space carve-out), #220 / #229 (sync IPC v0), #254 (cooperative scheduler block/wake), #240 / #247 (cap_handle revoke), #226 (manifest schema gate), #237 (cap_gate_check_handle), #211 / #221 (CAP:DENY marker conformance), #244 (CAP_AUDIT byte-exact fixture).

## Motivation

BUILD_ROADMAP §5.2 ("M2: Console service + Launcher + HelloApp") already has
all six structured pass markers wired into `build/scripts/test.sh` and green
on CI:

| Marker | Test source |
| --- | --- |
| `TEST:PASS:helloapp_allowed_console_write` / `TEST:PASS:helloapp_allow` | `tests/helloapp_allow_test.c` |
| `TEST:PASS:helloapp_denied_console_write` / `TEST:PASS:helloapp_deny`   | `tests/helloapp_deny_test.c` |
| `TEST:PASS:launcher_console_deny_without_grant`                         | `tests/launcher_console_test.c` |
| `TEST:PASS:launcher_console_allow_after_grant`                          | `tests/launcher_console_test.c` |
| `TEST:PASS:launcher_console_regression_bypass_denied`                   | `tests/launcher_console_test.c` |
| `TEST:PASS:launcher_console_revoke_restores_deny`                       | `tests/launcher_console_test.c` |

But every one of those tests was written **before** the M1 substrate landed.
They drive the cap_gate / cap_table surface directly with subject ids and
call `launcher_app_console_write` as a flat C function inside a single host
process. None of them ride on:

- the M1 process abstraction (`process_create` / `process_destroy`, #238),
- per-process `address_space_t` windows carved out of `.proc_arena` (#249),
- the cooperative scheduler + block/wake plumbing (#254),
- the synchronous IPC v0 primitive (`ipc_send` / `ipc_recv` / `ipc_call`,
  #220 / #229) and its handle-gated peers (`ipc_send_h` / `ipc_recv_h`,
  #246),
- the 32-bit capability handle layer (#233 / #237 / #240 / #247).

So M2 is technically green on paper while the next milestone (M3
fs_service, §5.3) cannot build on a stack that has never actually been
exercised end-to-end. This plan is the M2 analogue of plan #197 (cap table)
and plan #198 (process abstraction): a **plan-only doc** that enumerates the
PR-shaped slices needed to re-platform the existing M2 acceptance suite onto
the merged M1 substrate.

## Non-goals

- **Replacing** the existing host-fixture M2 tests. They stay as the fast
  pre-flight tier (`build/scripts/test.sh helloapp_allow` etc.). The
  re-platforming adds *peer* `_qemu` targets that ride on the real
  substrate. Pre-flight markers MUST keep their exact spelling — they are
  the contract observed by `validate_bundle.sh` (#110) and the daily
  capability matrix (#155 / #236).
- A new M2 module. No new console driver, no new launcher policy, no new
  HelloApp app code. The only new surface is the **IPC and cap_handle
  wiring** that puts the existing pieces on the M1 substrate.
- A real ELF or filesystem-backed HelloApp (M3+). HelloApp stays an
  in-kernel registered module spawned via `process_create`, same as the
  M1 IPC demo (`tests/m1_ipc_demo_test.c`, #255).
- ABI change. No `OS_ABI_VERSION` bump; no new entries in
  `capability_id_t`; no new `ipc_result_t` value. Every new error path
  reuses an existing enum value, exactly as #246 did for handle-gated IPC.
- Preemption, SMP, demand paging, ring transitions — explicit non-asks
  carried over from plan #198.

## Design surface

### Topology after re-platforming

```
┌───────────────────────────────────┐     ipc_send_h (CAP_CONSOLE_WRITE handle)
│  HelloApp (process_create, own    │ ─────────────────────────────────┐
│  address_space_t, subject = 3)    │                                  │
└───────────────────────────────────┘                                  ▼
                ▲                                       ┌──────────────────────────┐
                │ cap_handle_t minted by launcher       │  console service (in-    │
                │ per manifest, passed in HelloApp's    │  kernel module, owns     │
                │ initial IPC scratch slot              │  the console port,       │
                ▼                                       │  subject = 2)            │
┌───────────────────────────────────┐                   └──────────────────────────┘
│  Launcher (process_create, own    │                                  │
│  address_space_t, subject = 1)    │                                  ▼
│  - reads manifest (#226 schema)   │                              writes via the
│  - calls cap_table_grant +        │                              existing console
│    cap_handle_mint                │                              driver (no change)
│  - spawns HelloApp                │
└───────────────────────────────────┘
```

All three subjects (launcher, console, HelloApp) become **real M1 processes**
with their own PCB, address_space_t window, and per-process IPC scratch
buffer. Every cap_gate decision routes through `cap_gate_check_handle`
(#237), not `cap_check(subject, cap)`.

### What changes (per surface)

1. **Console service module** — new file `kernel/svc/console_svc.{c,h}`
   (matches the §5.2 wording "console service module" without renaming
   the existing console driver). It:
   - Allocates one well-known IPC port via `ipc_port_alloc` at boot.
     Owner = console subject (id 2). Send cap = `CAP_CONSOLE_WRITE`.
     Recv cap = a new well-known constant **reusing an existing
     capability id** (proposed: re-use `CAP_CONSOLE_WRITE` for both
     directions in the v0 slice — recv-side is owner-only and already
     gated by the port owner check, so no new cap is needed).
   - Runs as a registered M1 module: `process_create(name="console-svc",
     entry=console_svc_loop, ...)`. The loop calls `ipc_recv_h` in a
     bounded turn count (deterministic for tests), forwards the
     payload bytes to the existing console driver, and returns.
   - Exposes a stable port handle constant `CONSOLE_SVC_PORT_NAME` (in
     practice, the launcher learns the port id at boot via the module
     registry — no global rendezvous service yet).

2. **Launcher slice** — extend `kernel/user/launcher.{c,h}` (do not
   rename; the existing launcher stays the M2 mediation point):
   - New entry point `launcher_spawn_app_from_manifest(const
     launcher_manifest *m, process_id_t *out_pid)` that:
     1. Calls `process_create` to allocate a PCB + carve an
        `address_space_t` window.
     2. For each cap in `m->requested_caps` that policy approves
        (today: only `CAP_CONSOLE_WRITE`), calls
        `cap_table_grant_as_for_tests` then `cap_handle_mint` to
        produce a `cap_handle_t` bound to the new HelloApp subject.
     3. Writes the minted handle into the new process's IPC scratch
        slot (`address_space_t::ipc_scratch[0..7]`, little-endian
        encoding — re-using IPC scratch as the initial-handoff vector
        is the M1-cheap analogue of `argv`).
     4. Marks the PCB READY and returns its pid.
   - Keep `launcher_app_console_write` exactly as is for the existing
     host-fixture tests — it becomes a thin host-only shim.

3. **HelloApp** — extend `kernel/user/helloapp.{c,h}` (or land it new if
   it does not yet exist as a registered module — TBD by slice 4 author):
   - Entry: reads the cap_handle_t out of its address_space_t's
     ipc_scratch, looks up the console-svc port (via
     `module_registry_find_port("console-svc")`), and issues
     `ipc_send_h(handle, port, &msg)` with the banner payload.
   - On `IPC_OK`, emits `TEST:PASS:m2_helloapp_qemu_print` (new marker
     for the substrate-ridden form).

4. **Allow / deny / revoke re-platforming** — three new test binaries
   that live alongside the existing host-fixture ones:

   | New test binary | Mirrors host fixture | New marker(s) emitted |
   | --- | --- | --- |
   | `tests/m2_helloapp_allow_qemu_test.c` | `helloapp_allow_test.c` | `TEST:PASS:m2_helloapp_allow_qemu`, `TEST:PASS:m2_helloapp_allowed_console_write_qemu` |
   | `tests/m2_helloapp_deny_qemu_test.c`  | `helloapp_deny_test.c`  | `TEST:PASS:m2_helloapp_deny_qemu`, `TEST:PASS:m2_helloapp_denied_console_write_qemu` |
   | `tests/m2_launcher_console_qemu_test.c` | `launcher_console_test.c` | `TEST:PASS:m2_launcher_console_deny_without_grant_qemu`, `TEST:PASS:m2_launcher_console_allow_after_grant_qemu`, `TEST:PASS:m2_launcher_console_regression_bypass_denied_qemu`, `TEST:PASS:m2_launcher_console_revoke_restores_deny_qemu` |

   The `_qemu` suffix matches the §5.2 acceptance-demo convention used
   by `tests/m1_ipc_demo_test.c`. Each new test:
   - boots an in-kernel scenario via `process_create` (no QEMU image
     actually required for the host-side variant; the suffix denotes
     "rides on the real M1 substrate", not necessarily a full ISO
     boot — same convention as `m1_ipc_demo_test.c`),
   - drives the launcher → HelloApp → console-svc round trip,
   - asserts both the structured marker shape **and** the
     `CAP:DENY:<subject>:console_write:-` line on deny paths via the
     #221 conformance validator (`cap_deny_marker_validate`),
   - on the revoke path, calls `process_destroy(helloapp_pid)` and
     asserts the next `ipc_send_h` with the previously minted handle
     returns `IPC_ERR_CAP_DENIED` with a fresh canonical marker.

5. **Cap revocation on HelloApp exit** — this is the deferred-but-critical
   contract from §5.2's `launcher_console_revoke_restores_deny`. The new
   substrate makes it observable: `process_destroy(pid)` MUST call
   `cap_handle_revoke_subject(subject)` (#240) so that every
   previously-minted handle for the dead subject now fails
   `cap_gate_check_handle` with `CAP_GATE_ERR_REVOKED`. The new
   `m2_launcher_console_qemu_test` proves this by holding the
   pre-destroy handle in its own scope and re-attempting `ipc_send_h`
   after destroy.

### Manifest schema interaction (#226)

The launcher slice consumes the manifest schema v0 already gated by #226.
No schema changes — only the `requested_caps` field is read in this
slice; `caps_granted`, `ports`, and `entry` are left for M3+. The
`manifests/examples/helloapp.manifest.json` fixture is already shaped
for this (see #208).

### IPC scratch as initial-handoff vector

The M1 plan reserved `address_space_t::ipc_scratch` (#249) explicitly
as a per-process buffer of `IPC_MSG_PAYLOAD_MAX` bytes inside the
process's window. This plan commits the first concrete use: bytes
`[0..7]` of the new process's `ipc_scratch` hold a little-endian
`cap_handle_t` (the console-write handle minted by the launcher). This
is the M1-cheap stand-in for the M3+ `argv`-like initial-state contract;
when M3 lands real process loading it can replace this convention
without an ABI bump because nothing user-visible sees it (the entry
function is in-kernel C code in M1/M2).

Bytes `[8..]` of `ipc_scratch` remain available for the normal IPC send
buffer; the entry function is expected to copy the handle out and then
treat the scratch region as ordinary IPC space.

## ABI surface touched

- **None at the user/kernel boundary.** No additions to `docs/abi/`
  required by this plan; the substrate slices (#220, #237, #246, #249)
  already froze every relevant constant.
- **Internal-only**: a new `kernel/svc/console_svc.h` header declares the
  console service's module registry entry point. Not an ABI surface.

## Risks and explicit assumptions

- **Risk:** doubling the M2 test surface (host fixture + `_qemu` peer)
  doubles the maintenance load.
  *Mitigation:* the `_qemu` peers share most setup with `m1_ipc_demo_test.c`
  (process_create, ipc_port_alloc, cap_handle_mint). A small shared
  helper `tests/harness/m2_subjects.{c,h}` lands in slice 1 to keep the
  three new tests under ~120 LoC each.
- **Risk:** the "real M1 substrate" path discovers a contract gap (e.g.,
  `process_destroy` does **not** in fact call
  `cap_handle_revoke_subject` today).
  *Mitigation:* slice 4 is gated on a one-line audit of `process_destroy`
  before any test is written; if the call is missing, an additional
  follow-up issue (M2-SUBSTRATE-005) lands the fix as a one-line wire-in
  with a regression test, **before** the revoke `_qemu` peer.
- **Risk:** IPC scratch reuse as initial-handoff vector confuses the
  per-process `ipc_scratch` contract from #249.
  *Mitigation:* slice 2 lands a `docs/architecture/m1-m2-handoff.md`
  one-pager describing the convention, and the entry function copies
  the handle out before any IPC op writes to scratch.
- **Risk:** the console-svc port allocator runs before the IPC port
  table is initialised at boot.
  *Mitigation:* slice 1 wires `console_svc_init` into the existing M1
  boot order (after `ipc_port_table_init`, before `proc_init`'s
  module-registry walk).
- **Assumption:** all of #220, #229, #237, #240, #246, #247, #249, #254
  are landed on `main` (verified at plan-write time; this matches
  the §5.1 substrate listed in #259's body).

## Acceptance demo (maps directly to §5.2 validation bullets)

For each existing §5.2 marker, this plan adds a `_qemu` peer that rides
on the real M1 substrate. The pre-flight (host-fixture) markers stay
unchanged.

| §5.2 marker (pre-flight, kept verbatim) | Substrate-ridden peer (new in this plan) | M1 primitive exercised |
| --- | --- | --- |
| `TEST:PASS:helloapp_allowed_console_write` | `TEST:PASS:m2_helloapp_allowed_console_write_qemu` | `process_create` (#238) + `address_space_t` (#249) + `cap_handle_mint` (#247) + `ipc_send_h` (#246) → `console_svc` recv-loop |
| `TEST:PASS:helloapp_allow` | `TEST:PASS:m2_helloapp_allow_qemu` | scheduler block/wake (#254) round-trips one envelope between two real PCBs |
| `TEST:PASS:helloapp_denied_console_write` | `TEST:PASS:m2_helloapp_denied_console_write_qemu` | `cap_gate_check_handle` (#237) returns deny; canonical `CAP:DENY:<helloapp>:console_write:-` validated by #221 |
| `TEST:PASS:helloapp_deny` | `TEST:PASS:m2_helloapp_deny_qemu` | `ipc_send_h` returns `IPC_ERR_CAP_DENIED`; receiver stays blocked (no spurious wake) |
| `TEST:PASS:launcher_console_deny_without_grant` | `TEST:PASS:m2_launcher_console_deny_without_grant_qemu` | no handle minted → `ipc_send_h` rejected at handle resolve |
| `TEST:PASS:launcher_console_allow_after_grant` | `TEST:PASS:m2_launcher_console_allow_after_grant_qemu` | manifest-driven `launcher_spawn_app_from_manifest` mints handle → send succeeds |
| `TEST:PASS:launcher_console_regression_bypass_denied` | `TEST:PASS:m2_launcher_console_regression_bypass_denied_qemu` | bypass attempt = HelloApp invents a fabricated handle; `cap_gate_check_handle` rejects with `CAP_GATE_ERR_INVALID_HANDLE` |
| `TEST:PASS:launcher_console_revoke_restores_deny` | `TEST:PASS:m2_launcher_console_revoke_restores_deny_qemu` | `process_destroy(helloapp)` → `cap_handle_revoke_subject` (#240) → next `ipc_send_h` returns `IPC_ERR_CAP_DENIED` with fresh marker |

All `_qemu` markers are wired into `build/scripts/test.sh` and
`build/scripts/test.ps1` with `.shell_parity_allowlist` entries per
#156, and surface in the validator JSON report (#110) the same way the
M1 demo markers already do (#255).

## Follow-up implementation issues to file

These execute issues are the concrete units of work this plan unblocks.
Each is intended to be at most one PR / one agent session. Proposed
titles + done-when bullets:

1. **"M2-SUBSTRATE-001: console_svc in-kernel module + IPC port allocation (BUILD_ROADMAP §5.2)"**
   - Lands `kernel/svc/console_svc.{c,h}`, wires the recv-loop entry into the
     module registry, and the boot-order edge `ipc_port_table_init →
     console_svc_init → proc_init`.
   - Validator target: `console_svc_port_alloc` asserts the well-known port
     is allocated with the expected send_cap (`CAP_CONSOLE_WRITE`).
   - Adds the shared helper `tests/harness/m2_subjects.{c,h}`.

2. **"M2-SUBSTRATE-002: launcher_spawn_app_from_manifest + ipc_scratch handle handoff (BUILD_ROADMAP §5.2)"**
   - Extends `kernel/user/launcher.{c,h}` with the new entry point.
   - Defines the `address_space_t::ipc_scratch[0..7] = LE(cap_handle_t)`
     convention; lands `docs/architecture/m1-m2-handoff.md` describing it.
   - Validator target: `launcher_spawn_handoff` asserts the spawned PCB
     reads back the handle byte-exactly.

3. **"M2-SUBSTRATE-003: HelloApp module riding on substrate + allow/deny `_qemu` tests (BUILD_ROADMAP §5.2)"**
   - Adds (or extends) `kernel/user/helloapp.{c,h}` as a registered M1
     module.
   - Lands `tests/m2_helloapp_allow_qemu_test.c` +
     `tests/m2_helloapp_deny_qemu_test.c` with markers from the table
     above; wires both into `test.sh` / `test.ps1`.

4. **"M2-SUBSTRATE-004: launcher_console `_qemu` peer (all four §5.2 launcher-mediation markers) (BUILD_ROADMAP §5.2)"**
   - Lands `tests/m2_launcher_console_qemu_test.c` covering the four
     `launcher_console_*` peers from the table above.
   - Gated on a one-line audit of `process_destroy` calling
     `cap_handle_revoke_subject` (see Risks). If missing, this slice
     files M2-SUBSTRATE-005 as a precondition.

5. **"M2-SUBSTRATE-005 (conditional): wire `cap_handle_revoke_subject`
   into `process_destroy` (BUILD_ROADMAP §5.2)"**
   - Only filed if slice 4's audit shows the call is missing. One-line
     wire-in + targeted regression test
     `tests/proc_destroy_revokes_handles_test.c`.

A separate, non-blocking housekeeping issue (M2-SUBSTRATE-DOC) covers
the eventual `docs/architecture/m2-substrate-ridden-topology.md`
update once all five slices land — out of scope for this plan.

## Out of scope (explicit non-asks)

- ELF / filesystem-backed HelloApp loading (M3).
- A new console driver or any change to the existing one.
- New capabilities, new error enum values, or any `OS_ABI_VERSION` bump.
- M3 fs_service work (separate plan thread; see #128 / #14X).
- M4 capability broker (separate plan thread; see #127).
- Removal of the existing host-fixture M2 tests. They remain as the
  fast pre-flight tier.
