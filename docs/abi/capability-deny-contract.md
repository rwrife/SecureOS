# Capability-Denied Error + Log Marker Contract

Status: DRAFT (spec-only — no service implements this yet; see "Implementation tracking" below)
Owners: capability subsystem (`kernel/cap/`), launcher/process (`kernel/user/launcher_exec.c`), service authors (M2 console, M3 fs, M4 broker).
Tracks: #164. Coordinates with the broader ABI reference in #93 / PR #145.

## 1. Why this exists

`BUILD_ROADMAP.md` §5.2 (M2 acceptance) requires both a grant path and a
**deny path** for HelloApp. The same shape repeats for M3 fs (#108) and M4
broker (#115). Without a single contract, each slice would invent its own
return code, log marker, and audit shape, and the deny-path tests in #92,
#108, and #115 would each lock in a slightly different pattern that we
then have to retroactively reconcile.

This document defines one canonical contract that every deny-path service
emission MUST follow. It is intentionally narrow: it does not implement
anything; it only fixes the wire shape so the M2/M3/M4 slices and their
acceptance tests agree.

## 2. Scope

In scope:

- the single capability-denied return code/errno surfaced to user code,
- the structured serial log marker emitted by the kernel/launcher when a
  capability check denies an attempted operation,
- the synchronous vs. asynchronous delivery rule for each service,
- how the deny event maps onto the existing CAP-009..CAP-020 audit ring
  (`cap_audit_event_t` in `kernel/cap/capability.h`) without changing its
  schema.

Out of scope (and explicitly NOT changed by this doc):

- the audit ring schema itself (frozen by #60),
- the set of capability ids (`capability_id_t` in `kernel/cap/capability.h`),
- per-service policy for *which* capability gates an operation (each slice
  owns that),
- user-visible UX of fallbacks beyond what is listed in §5.

## 3. Return code / errno

A capability denial surfaces to the calling user app as exactly one value:

- user API surface (`user/include/secureos_api.h`):
  **`OS_STATUS_DENIED`** (value `1`, already defined).
- kernel internal surface (`kernel/user/launcher_exec.h`):
  **`PROCESS_ERR_DENIED`** (value `4`, already defined).
- capability internal surface (`kernel/cap/capability.h`):
  **`cap_check()` returning `CAP_ERR_MISSING`** is the kernel-side trigger.
  Services MUST translate `CAP_ERR_MISSING` into `PROCESS_ERR_DENIED` at
  the launcher boundary and into `OS_STATUS_DENIED` at the user boundary.

There is exactly **one** capability-denied return value per layer. Services
MUST NOT invent service-specific deny codes (no
`OS_STATUS_FS_DENIED`, no `OS_STATUS_NET_DENIED`). If a richer error
classification is needed later, it is added as a separate field, not as a
new top-level status value.

Rationale: the user-app contract is "did the kernel let me?" — a single
boolean-ish status. The audit log (§5) carries the structured detail.

## 4. Serial log marker

Every capability denial MUST emit exactly one serial log line of the form:

```
CAP:DENY:<actor_subject_id>:<capability_id>:<resource>
```

Field grammar (ASCII only, no spaces inside fields):

- `<actor_subject_id>` — decimal `cap_subject_id_t` of the subject the
  kernel evaluated the check against (the launched app's subject id, not
  the launcher's). Always present.
- `<capability_id>` — symbolic name from `capability_id_t`, lowercased,
  with the `CAP_` prefix stripped and underscores preserved. Examples:
  `console_write`, `fs_read`, `fs_write`, `network`,
  `capability_admin`. Always present.
- `<resource>` — the smallest meaningful identifier for the denied
  operation: a path for fs ops, a URL scheme/host for net ops, a target
  subject id for broker ops, the literal `-` when the operation has no
  resource handle (e.g. `console_write`). Always present, never empty,
  no colons (`:`) — replace any colon in a resource with `_` before
  emission.

Examples (one per current M2/M3/M4 driver):

```
CAP:DENY:42:console_write:-
CAP:DENY:42:fs_read:/os/notes.txt
CAP:DENY:42:broker_share:subject=17
```

The marker family is intentionally aligned with the existing
`TEST:PASS:` / `TEST:FAIL:` markers documented in
`docs/CODING_CONVENTIONS.md` §"Test markers" and the in-flight `CAP:`
audit-summary markers from CAP-009..CAP-020 — same `<DOMAIN>:<KIND>:`
prefix discipline, colon-delimited fields, newline-terminated.

A grant path MUST NOT emit `CAP:DENY:`. A deny path MUST NOT emit
`TEST:PASS:` for the underlying operation; the *test harness* may still
emit `TEST:PASS:<deny-test-name>` once it has observed the expected
`CAP:DENY:` line (this is how the M2/M3/M4 deny-path tests assert).

## 5. Delivery model — sync vs. async

The deny signal reaches the calling app in one of two modes. Each service
declares which mode it uses, and the test that drives it asserts against
that mode.

| Service              | Capability         | Mode | Notes                                                                 |
|----------------------|--------------------|------|-----------------------------------------------------------------------|
| `os_console_write`   | `CAP_CONSOLE_WRITE`| sync | Wrapper returns `OS_STATUS_DENIED`; nothing is written to console.    |
| `os_fs_read_file`    | `CAP_FS_READ`      | sync | Wrapper returns `OS_STATUS_DENIED`; `out_buffer[0] = '\0'`.           |
| `os_fs_write_file`   | `CAP_FS_WRITE`     | sync | Wrapper returns `OS_STATUS_DENIED`; no partial write is performed.    |
| `os_net_http_get` / `os_net_https_get` | `CAP_NETWORK` | sync | Wrapper returns `OS_STATUS_DENIED`; scheme gate (#79) is the trigger. |
| broker `share` / `revoke` | `CAP_CAPABILITY_ADMIN` | sync | Returns `OS_STATUS_DENIED`; no grant/revoke side effect.          |
| `os_event_*`         | `CAP_EVENT_*`      | async (fallback) | Subscription denial drops events silently for the subject; one `CAP:DENY:` is emitted at subscribe time, not per dropped event. |
| virtual-graphics framebuffer write | `CAP_GFX_FRAMEBUFFER` | sync | Gate primitive `cap_gfx_framebuffer_gate` lands in #349; emits `CAP:DENY:<sid>:gfx_framebuffer:-\n` on deny. HAL/driver call-site wiring + `_qemu` peers tracked as follow-ups in #349. |
| PS/2 keyboard byte queue | `CAP_INPUT_KEYBOARD` | sync | Gate primitive `cap_input_keyboard_gate` lands in #349; emits `CAP:DENY:<sid>:input_keyboard:-\n` on deny. Driver call-site wiring tracked as a follow-up in #349. |
| PS/2 mouse byte queue | `CAP_INPUT_MOUSE` | sync | Numeric id 17 — renamed from `CAP_MOUSE` by #348 with id preserved (append-only). Gate primitive `cap_input_mouse_gate` lands in #349; emits `CAP:DENY:<sid>:input_mouse:-\n` on deny. |

Default for new services: **sync**. Async fallback is reserved for
push-style services where blocking the caller would be incorrect; adding
a new async-fallback service requires updating this table.

## 6. Audit ring mapping

The deny event MUST be recorded into the existing capability audit ring
without schema change:

- `cap_audit_event_t.operation`     = `CAP_AUDIT_OP_CHECK`
  (deny is a check that returned non-ok; `GRANT` / `REVOKE` are reserved
  for mutation ops per `kernel/cap/capability.c`).
- `cap_audit_event_t.actor_subject_id` = the subject that attempted the op.
- `cap_audit_event_t.subject_id`    = same as `actor_subject_id` for the
  current check-on-self model used by `cap_check()`.
- `cap_audit_event_t.capability_id` = the `capability_id_t` that was checked.
- `cap_audit_event_t.result`        = `CAP_ERR_MISSING`
  (this is the existing deny signal; any non-`CAP_OK` result counts as a
  deny entry for replay purposes).
- `cap_audit_event_t.sequence_id`   = the next ring sequence id, assigned
  by `cap_audit_record()` in `kernel/cap/capability.c`. Not modified by
  this contract.

Existing checkpoint / seal / dropped-count semantics are unchanged. The
audit ring already records `cap_check()` outcomes via `cap_audit_record`
(see `kernel/cap/capability.c:259`), so the audit half of this contract
is satisfied by the current code path; services only need to emit the
serial `CAP:DENY:` marker described in §4 in addition to whatever they
already do on `cap_check() != CAP_OK`.

## 7. Worked examples (drivers for #92 / #108 / #115)

### 7.1 `console.write` deny (drives #92, M2)

Setup: HelloApp (subject id 42) is launched without `CAP_CONSOLE_WRITE`.
HelloApp calls `os_console_write("hello\n")`.

Expected observable bytes on the serial test bus, in order:

```
CAP:DENY:42:console_write:-
TEST:PASS:hello_console_write_deny
```

Expected user-app return: `OS_STATUS_DENIED`.
Expected audit ring tail entry: `operation=CHECK, capability_id=CAP_CONSOLE_WRITE, result=CAP_ERR_MISSING, actor_subject_id=42, subject_id=42`.
Console output of HelloApp's payload: **none** (no `hello\n` on serial or VGA).

### 7.2 `fs.read` deny (drives #108, M3)

Setup: app subject id 42 holds neither `CAP_FS_READ` nor `CAP_FS_WRITE`.
App calls `os_fs_read_file("/os/notes.txt", buf, sizeof buf)`.

Expected serial:

```
CAP:DENY:42:fs_read:/os/notes.txt
TEST:PASS:fs_read_deny_no_cap
```

Expected return: `OS_STATUS_DENIED`; `buf[0] == '\0'`.
No bytes of `/os/notes.txt` are exposed on serial or in the buffer.

### 7.3 `broker.share` deny (drives #115, M4)

Setup: app subject id 42 lacks `CAP_CAPABILITY_ADMIN` and attempts to
share `CAP_FS_READ` with target subject id 17.

Expected serial:

```
CAP:DENY:42:capability_admin:broker_share=cap=fs_read,target=17
TEST:PASS:broker_share_deny_no_admin
```

(The `<resource>` field encodes the operation, the capability being
shared, and the target subject — colon-free per §4.)

Expected return: `OS_STATUS_DENIED`. No grant entry appears in either
subject's capability table; no `CAP_AUDIT_OP_GRANT` event is recorded
(only the `CAP_AUDIT_OP_CHECK` deny event from §6).

### 7.4 `app_exec` deny (launcher spawn pre-check, drives #410 / #532)

Setup: app subject id 5 lacks `CAP_APP_EXEC` and invokes the
`app_native_process_spawn` bridge slot
(`kernel/user/launcher_exec.c`, M7-TOOLCHAIN-003 #422 / PR #427) with
path `/apps/hello.bin`. The launcher emits the canonical deny marker
*before* `process_run` touches the filesystem so the audit-ring
scanner sees a stable `app_exec:<resource>` line for the
`launch.denied` invariant in plan #403 P4 (BUILD_ROADMAP §5.2) even
for non-existent binaries.

Expected serial:

```
CAP:DENY:5:app_exec:/apps/hello.bin
TEST:PASS:app_native_process_spawn_deny_marker_canonical_shape
```

The `<resource>` field is the requested path. Any byte that
`cap_deny_marker_format` would reject (`:`, `\n`, non-printable, or
non-ASCII) is rewritten to `_` by the launcher pre-check so a
pathological path still produces a parseable marker; the sanitizer is
pinned by `tests/app_native_process_spawn_deny_marker_test.c`. The
emission shares the `app_exec:<resource>` shape used by
`proc_emit_table_full_deny_marker` (§7 in `kernel/proc/process.c`,
`resource=proc_table_full`) so a single `CAP:DENY:*:app_exec:*` grep
picks up both policy and exhaustion denies.

Expected return: bridge slot returns `1` (mapped to
`PROCESS_ERR_CAPABILITY` by the userland `os_process_spawn` wrapper);
no child process is created.

## 8. Conformance checklist (for slice authors)

A service satisfies this contract when:

1. On `cap_check(subject, cap) != CAP_OK` it emits one serial line
   `CAP:DENY:<actor>:<cap>:<resource>\n` per §4 grammar.
2. It returns `OS_STATUS_DENIED` to the user wrapper (or, for kernel
   internals, `PROCESS_ERR_DENIED`).
3. It performs **no** side effect: no partial write, no partial mutation,
   no caller observable bytes from the denied resource.
4. It relies on the existing audit ring entry from `cap_check()` /
   `cap_audit_record()`; it does NOT create a parallel audit channel.
5. The acceptance test (#92 / #108 / #115 / …) asserts on the exact
   `CAP:DENY:` line above and on the absence of the grant-path bytes.

### 8.1 Conformance test (single source of truth)

Issue #211 adds `tests/cap_deny_marker_shape_test.c` plus the
`kernel/cap/cap_deny_marker.{h,c}` formatter and validator. **All deny-path
services MUST produce their `CAP:DENY:` line via `cap_deny_marker_format()`
and register their exemplar `(actor, capability, resource)` triple in the
`drivers[]` table of the conformance test.** Services MUST NOT invent
standalone grep regexes for the marker shape — that is exactly the drift
this contract exists to prevent.

Run via:

```
./build/scripts/test.sh cap_deny_marker_shape          # POSIX
./build/scripts/test.ps1 cap_deny_marker_shape         # Windows
```

The target is wired into `validate_bundle.sh` `TEST_TARGETS` so the
validator JSON report records its result on every CI run. Adding a new
deny path is therefore a three-line patch to `drivers[]`, not a new test.

## 9. Implementation tracking

- **#92** — M2 `console.write` deny path. First consumer of §7.1.
- **#108** — M3 fs deny-path + ephemeral-reset acceptance tests. Consumer of §7.2.
- **#115** — M4 broker allow/deny/revoke acceptance tests. Consumer of §7.3.
- **#93 / PR #145** — ABI reference. If #93 lands first this doc moves under
  `docs/abi/` as a subsection rather than a standalone file; the contract
  shape does not change.
- **#60** — audit ring guardrail. This doc does not modify the audit
  schema; §6 only specifies how existing fields are populated on deny.

## 10. Versioning

This contract is part of the SecureOS ABI surface and is therefore tied
to `OS_ABI_VERSION` (see `BUILD_ROADMAP.md` §7 and issue #150). At
`OS_ABI_VERSION=0` the marker grammar, return code, and audit mapping
above are unstable: additive changes (new fields appended after
`<resource>` with a leading `:`) MAY land; renames or removals require
bumping `OS_ABI_VERSION`. A grammar change MUST update §4 and §7 in the
same PR.

Last verified against commit: 80591f22d3
