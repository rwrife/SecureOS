# sosh Capability Surface + Sandbox Contract (v0)

| Field         | Value                                                          |
| ------------- | -------------------------------------------------------------- |
| Owner         | sosh / scripting subsystem (`user/libs/soshlib/`, `user/apps/sosh/`) |
| Status        | DRAFT (spec-only — first slice of issue [#351](https://github.com/rwrife/SecureOS/issues/351); enforcement + tests tracked as follow-ups, see §7) |
| Applies to    | `OS_ABI_VERSION = 0`                                           |
| Tracking issue| [#351](https://github.com/rwrife/SecureOS/issues/351) (gap from merged PR [#336](https://github.com/rwrife/SecureOS/pull/336)) |
| Last reviewed | 2026-05-28                                                     |

## 1. Why this exists

PR #336 landed `sosh` (the SecureOS Shell scripting language) without
declaring its capability surface. Today a script executed by the `sosh`
host can implicitly invoke any builtin or external command the host
process holds capabilities for, with no per-script manifest and no gate
distinguishing the script-author's authority from the host's. That
directly contradicts the zero-trust / explicit-consent invariants in
[`README.md`](../../README.md) ("Design Principles") and AGENTS.md, and
will block sosh from being usable inside the M6 SDK
([`BUILD_ROADMAP.md`](../../BUILD_ROADMAP.md) §5.6) where third-party
apps appear.

This document fixes the contract for the v0 ABI so the enforcement
follow-up (#351 Done-when bullets 2-3) and any manifest-validator work
(#312, #285) have a single normative reference. It does **not** change
the set of `CAP_*` ids — see §3 for the ADR on why sosh reuses existing
capability identifiers rather than introducing `CAP_SOSH_*`.

## 2. Scope

In scope:

- which sosh builtins and dispatch paths are side-effecting and therefore
  MUST go through a capability check before executing,
- which existing `CAP_*` id gates each one,
- how a sosh script declares its requested capabilities (header pragma
  syntax + sidecar manifest layout),
- the canonical `CAP:DENY:` marker shape emitted on a script-side denial.

Out of scope (and explicitly NOT redefined here):

- the capability id registry itself — `docs/abi/capability-registry.json`
  remains the single source of truth,
- the deny-marker grammar — frozen by
  [`docs/abi/capability-deny-contract.md`](capability-deny-contract.md) §4,
- the manifest schema — extended additively by
  [`manifests/schema/v0.json`](../../manifests/schema/v0.json) per #285,
- syntax of the sosh language itself — see
  [`plans/2026-05-25-scripting-language.md`](../../plans/2026-05-25-scripting-language.md).

## 3. ADR: sosh reuses existing `CAP_*` ids; no `CAP_SOSH_*` namespace

**Decision:** sosh script-level capability checks reuse the existing
`CAP_*` ids and emit the existing `CAP:DENY:<sid>:<cap_name>:<resource>`
markers. We do **not** introduce a parallel `CAP_SOSH_*` namespace.

**Why:**

1. **Authority is in the underlying syscall, not the script verb.**
   When a script runs `cat /apps/x.bin`, the side effect is `os_fs_read_file`,
   which is already gated by `CAP_FS_READ`. Inventing `CAP_SOSH_CAT` would
   double-gate the same operation and force every future kernel capability
   change (#234 registry, M5 cascade #313) to update two tables.
2. **Zero ABI surface bump.** No new `numeric_id` entries means no
   `OS_ABI_VERSION` interaction and no new rows in
   `capability-registry.json`. The freshly-merged registry validator
   (#234) stays untouched.
3. **Matches the broker precedent.** `cap_broker_*` does not have its own
   capability id either — broker authority is bound to the *subject*
   ("is this subject the broker?") not to a cap. sosh is the same
   pattern: the sosh host process is just a subject; the script's
   effective authority is the **intersection** of (a) the host subject's
   granted caps and (b) the caps the script's manifest requests.
4. **Auditability.** A `CAP:DENY:<sid>:fs_read:/apps/x.bin` marker in
   the serial log unambiguously tells an operator that an fs_read was
   denied, regardless of whether the actor was a sosh script, a C app,
   or the broker. A separate `sosh_cat` marker would just paper over
   the real reason.

**Alternative considered:** a `CAP_SCRIPT_EXEC` umbrella cap (one
boolean per subject: "may run sosh scripts at all"). Rejected for v0:
the launcher already gates `apps/sosh.bin` execution via the existing
manifest pipeline, so the umbrella adds a check at a layer that is
already checked. We may revisit if/when sosh grows an eval-from-string
builtin that bypasses the launcher.

**Consequence:** every side-effecting sosh builtin in §4 lists the
*existing* cap id it must defer to. Implementation slice (#351 follow-up)
wires `cap_gate_check_handle` against that id; no kernel header change
needed.

## 4. Side-effecting builtins + required capabilities

The following sosh dispatch paths are side-effecting. Each MUST invoke
`cap_gate_check_handle` (or its host-side equivalent — see §5) against
the listed capability *before* executing the underlying syscall. On
denial it MUST emit the canonical `CAP:DENY:` marker shown.

| sosh surface         | Underlying syscall            | Required cap        | Deny marker (`<sid>` = script subject id)        |
| -------------------- | ----------------------------- | ------------------- | ------------------------------------------------ |
| `echo`               | `os_console_write`            | `CAP_CONSOLE_WRITE` | `CAP:DENY:<sid>:console_write:-`                 |
| (any builtin output) | `os_console_write`            | `CAP_CONSOLE_WRITE` | `CAP:DENY:<sid>:console_write:-`                 |
| `cat <path>`         | `os_fs_read_file`             | `CAP_FS_READ`       | `CAP:DENY:<sid>:fs_read:<path>`                  |
| `source <path>`      | `os_fs_read_file` (`__cat_raw`)| `CAP_FS_READ`      | `CAP:DENY:<sid>:fs_read:<path>`                  |
| `ls <path>`          | `os_fs_list_dir`              | `CAP_FS_READ`       | `CAP:DENY:<sid>:fs_read:<path>`                  |
| `exists <path>` (in conditional) | `os_fs_list_dir` / `os_fs_read_file` | `CAP_FS_READ` | `CAP:DENY:<sid>:fs_read:<path>` |
| `> <path>` / future `write` builtin | `os_fs_write_file` | `CAP_FS_WRITE`      | `CAP:DENY:<sid>:fs_write:<path>`                 |
| external command (`apps/foo.bin args...`) | `process_create` via launcher | `CAP_APP_EXEC` of `<binary>` | `CAP:DENY:<sid>:app_exec:<binary>` |
| `export VAR=...` (env mutate) | env service write | `CAP_ENV_WRITE` (if defined) — otherwise host-only (see §6) | `CAP:DENY:<sid>:env_write:<var>` |

Pure-language constructs (`set`, `if`/`elif`/`else`/`end`, `while`,
`for`, arithmetic and string ops, positional args, substring slicing)
are **non side-effecting** and are NOT gated — they cannot escape the
interpreter sandbox and have no kernel surface to deny. The `exit` /
`return` builtins likewise terminate only the script and require no
capability.

> Notes
>
> - `CAP_APP_EXEC` is the per-binary execution gate already enforced by
>   the launcher (`kernel/user/launcher_exec.c`); sosh inherits this
>   check for free as long as it spawns external commands through the
>   launcher syscall instead of an in-process shortcut.
> - The `env` `CAP_ENV_WRITE` row is conditional: if no env-write
>   capability exists at the time enforcement lands, the implementation
>   slice MUST either (a) add a `CAP_ENV_WRITE` registry entry first
>   (separate cap-registry PR), or (b) make `export` a host-only no-op
>   for sandboxed scripts and document that here in the next revision.
>   The soshlib-level enforcement now routes the env-service write
>   through the abstract `SOSH_CAP_ENV_WRITE` tag (see §7 `export`
>   slice), leaving the embedder free to take path (b) by returning a
>   non-zero deny rc so `export` becomes a host-only no-op for
>   sandboxed scripts — the in-process `set`-equivalent variable
>   update still happens (it is not side-effecting and has no kernel
>   surface). Path (a) remains the long-term option once an
>   env-service capability exists; until then the registry stays
>   untouched (consistent with the §3 ADR).

## 5. How a script declares its requested capabilities

A sosh script may run in one of two host modes. The contract for each:

### 5.1 Host-process mode (current — sosh as an app)

When `apps/sosh.bin` is launched from the launcher, its **own**
manifest (e.g. `manifests/examples/sosh.json`, see §5.3) lists the
union of capabilities any script it may run will need. The host's
effective subject id is what `cap_gate_check_handle` evaluates against;
the script does NOT get its own `cap_subject_id_t` in this mode. This
is the same model as a single-binary app — sosh is responsible for not
exceeding its own granted set.

This is the v0 default. It is sufficient for trusted boot scripts
(`/scripts/init.sosh`) and developer toys, and it is what enforcement
slice (#351 follow-up) targets first.

### 5.2 Per-script subject mode (v0.1+, for the M6 SDK)

For untrusted scripts (third-party `.sosh` files dropped on disk), the
launcher MUST spawn a *new* subject id for the script and intersect the
host's cap set with the script's declared request set. The declaration
takes one of two forms:

**(a) Header pragma** — first non-shebang, non-blank line of the
script:

```sosh
#!/sosh
#@caps CAP_CONSOLE_WRITE CAP_FS_READ
echo "hello"
```

Grammar: `^#@caps( +CAP_[A-Z0-9_]+)+$`. The pragma is parsed once at
load time; any cap not in the host's granted set is denied at load,
not at first use. Unknown cap names (not in
`capability-registry.json`) cause the script to fail load with
`OS_STATUS_DENIED`.

**(b) Sidecar manifest** — a file named `<script>.manifest.json`
next to the script, conforming to
[`manifests/schema/v0.json`](../../manifests/schema/v0.json) with the
additive optional field `app.kind = "sosh_script"` and `app.binary`
pointing at the `.sosh` source. Sidecar wins over header pragma if both
are present (consistent with the launcher manifest precedence rule
already in `docs/abi/manifest.md`).

Per-script subject mode requires #312 / #285 manifest-validator
plumbing — it is deliberately NOT a v0 done-when item; this section
fixes the ABI so the M6 SDK slice has a stable target.

### 5.3 Example: sosh host manifest

A new
[`manifests/examples/sosh.json`](../../manifests/examples/sosh.json) (to
be added by the enforcement follow-up slice) reflects the host-process
mode authority:

```json
{
  "manifest_version": 0,
  "os_abi_version": 0,
  "app": {
    "id": "sosh",
    "version": "0.1.0",
    "subject_id": 5,
    "binary": "apps/sosh.bin",
    "signer_key_id": "secureos-dev-key-1"
  },
  "capabilities": {
    "request": ["CAP_CONSOLE_WRITE", "CAP_FS_READ"],
    "optional": ["CAP_FS_WRITE", "CAP_APP_EXEC"]
  },
  "launcher": {
    "auto_grant_at_launch": ["CAP_CONSOLE_WRITE", "CAP_FS_READ"],
    "require_user_confirm": ["CAP_FS_WRITE", "CAP_APP_EXEC"]
  }
}
```

`auto_grant_at_launch` covers the read-only "boot script" use case; any
fs-mutating or process-spawning script MUST trigger explicit user
confirmation, matching the existing launcher policy from
[`docs/abi/manifest.md`](manifest.md).

## 6. Deny semantics

When a sosh builtin's gate check fails:

1. The kernel emits the `CAP:DENY:` line per §4 *exactly once* per
   denied attempt (no per-line spam from loops — sosh MUST short-circuit
   the loop body on a denied builtin and set `$?` to a non-zero exit
   code so the script can `if` on it).
2. The builtin returns `OS_STATUS_DENIED` from its `state->exec`
   callback. The interpreter records the exit code (already wired via
   `sosh_vars_set_exit_code`) and continues to the next line — it does
   NOT abort the whole script, so a script can recover with
   `if $? != 0`.
3. No output is written from the denied builtin. (For `echo`, this
   means an unauthorized script cannot leak text to the console even on
   the *failed* path.)
4. The audit ring (`cap_audit_event_t`) MUST receive a `CAP-018`
   "denied operation" entry per
   [`docs/abi/capability-deny-contract.md`](capability-deny-contract.md)
   §5 — same shape as the M2/M3/M4 deny paths, no sosh-specific schema
   extension.

### 6.1 Audit integration (embedder follow-up)

The `cap_check` deny path in soshlib is currently a *script-side* gate
only: it short-circuits the builtin and propagates the deny rc into
`$?`, but it does NOT itself append to the kernel capability-audit
ring. That wiring is intentionally left to the **embedder** (the
in-tree sosh host, future per-script launcher, or test fixture), to
keep `user/libs/soshlib/` kernel-cap-agnostic per §5.1. This mirrors
the M4 broker→audit split that was closed by issue [#311](https://github.com/rwrife/SecureOS/issues/311)
(which flipped `audit_*_recorded` SKIPs to PASS once a broker embedder
started feeding `cap_audit_record_deny`).

**Embedder contract (normative):**

- An embedder that owns a sosh subject id MAY wire its
  `sosh_cap_check_fn` deny path into
  `cap_audit_record_deny(subject_id, cap_id, "-")` (or the
  resource-bearing equivalent, when the gated builtin carries one).
- The canonical audit-ring line shape is
  `CAP:DENY:<sid>:<sosh-cap>:-\n` per
  [`docs/abi/capability-deny-contract.md`](capability-deny-contract.md)
  §4.3. The `<sosh-cap>` token is the contract §4 spelling (e.g.
  `console_write`, `fs_read`, `fs_write`, `app_exec`, `env_write`).
  No sosh-specific marker grammar is introduced.
- No new `SOSH_CAP_*` ids and no `OS_ABI_VERSION` bump are required to
  wire this — `cap_audit_record_deny(...)` is already in tree (used by
  the M4 broker after #311) and the soshlib API surface is unchanged.

**SKIP discipline until an in-tree embedder lands:** every sosh
host-side deny test (see §7 "Implementation tracking" for the list)
emits the marker

```
TEST:SKIP:<test>:audit_deny_recorded:sosh_audit_unwired_pending_issue_389
```

exactly once on the deny path. The `<test>` token matches the test's
existing `TEST:START:` / `TEST:PASS:` family root (e.g.
`sosh_cap_deny`, `sosh_cap_cat_ls`, `sosh_cap_write_append`,
`sosh_cap_export`, `sosh_cap_exists`, `sosh_cap_source_exec`). The
marker is purely informational — `build/scripts/test_sosh_cap_*.sh`
still `grep -q` the existing required PASS lines, and no validator
asserts the SKIP shape today. Once an in-tree sosh embedder begins
feeding deny events to the audit ring, the marker flips to a
`TEST:PASS:<test>:audit_deny_recorded` assertion driven against the
ring contents, mirroring the M4 #311 transition.

## 7. Implementation tracking

This document is the first done-when bullet of #351. The remaining
bullets are tracked as follow-up slices and reference this contract as
their normative source:

- **Slice 2 (in progress):** `user/libs/soshlib/sosh_eval.c` consults
  an embedder-supplied `sosh_cap_check_fn` (see
  `user/libs/soshlib/sosh_builtins.h`) before executing each
  side-effecting builtin. **`echo` → `SOSH_CAP_CONSOLE_WRITE`,
  `source <path>` → `SOSH_CAP_FS_READ`,
  `exists <path>` → `SOSH_CAP_FS_READ`, external-command dispatch
  (`apps/foo.bin ...`) → `SOSH_CAP_APP_EXEC`, the FS-read
  external-command surfaces `cat <path>` / `ls <path>` →
  `SOSH_CAP_FS_READ`, the FS-write external-command surfaces
  `write <path> <content>` / `append <path> <content>` →
  `SOSH_CAP_FS_WRITE` (each with `resource = <path>`), and the
  `export VAR=value` builtin's env-service write →
  `SOSH_CAP_ENV_WRITE` with `resource = <var>` are wired
  today**; this closes the §4 enumeration — every side-effecting
  row in the contract now has a soshlib-level gate. The
  `cat` / `ls` / `write` / `append` routing is keyed on the `cmd`
  token at the external-command dispatch site — sosh keeps no
  separate built-in table for them — so the gate matches the §4
  contract's *underlying syscall* axis (`os_fs_read_file` /
  `os_fs_list_dir` / `os_fs_write_file`) rather than the dispatch
  axis (same precedent the `source` slice established).
  soshlib stays kernel-cap-agnostic: the embedder (kernel host, test
  fixture, or future per-script launcher) owns the mapping from the
  abstract `SOSH_CAP_*` tag to the matching `CAP_*` and is
  responsible for emitting the canonical `CAP:DENY:` marker + audit
  record per §6.
- **Slice 3 (partially shipped):** `tests/sosh_cap_allow_test.c` +
  `tests/sosh_cap_deny_test.c` cover the `echo` → console_write
  surface end-to-end against the soshlib enforcement contract:
  allow emits + sets `$? = 0`; deny short-circuits, leaks no text,
  propagates the deny rc into `$?`, and does NOT abort the script
  (per §6 bullets 1–3). Wired into `build/scripts/test.sh` as
  `sosh_cap_allow` / `sosh_cap_deny`. The `_qemu` peers (driving a
  real launched sosh subject through `cap_console_write_gate`) are
  the follow-up once the kernel-side embedder shim lands.
- **Slice 4 (optional, v0.1):** per-script subject mode wiring (§5.2)
  + manifest-validator support, coordinating with #312 / #285.

`docs/abi/capability-registry.json` is **not** modified by this slice
— the ADR in §3 explicitly chose to reuse existing caps. The
"or an explicit ADR explaining why sosh reuses existing caps" branch
of #351's last done-when bullet is satisfied by §3.

### 7.1 §4 enforcement-status matrix

The enforcement status of each §4 row at this revision is:

| §4 row                                       | Enforcement status                                                                 |
| -------------------------------------------- | ---------------------------------------------------------------------------------- |
| `echo` (`SOSH_CAP_CONSOLE_WRITE`)            | enforced — `sosh_cap_allow` / `sosh_cap_deny` (slice 2/3 of #351, PR #358)         |
| `cat <path>` / `ls <path>` (`SOSH_CAP_FS_READ`) | enforced — `sosh_cap_cat_ls`                                                    |
| `source <path>` (`SOSH_CAP_FS_READ`)         | enforced — `sosh_cap_source_exec` (slice 3/3 of #351, refs #371)                   |
| `exists <path>` (`SOSH_CAP_FS_READ`)         | enforced — `sosh_cap_exists`                                                       |
| `write <path>` / `append <path>` (`SOSH_CAP_FS_WRITE`) | enforced — `sosh_cap_write_append`                                       |
| external command (`SOSH_CAP_APP_EXEC`)       | enforced — `sosh_cap_source_exec` (soshlib gate, slice 3/3 of #351); embedder fall-through to `os_process_spawn` in `user/apps/sosh/main.c` pinned by `sosh_external_exec` (#493 — closes the `sosh> hello` no-op gap; depends on #422). Probe order: `/apps/<cmd>`, `/apps/dev/<cmd>`, then `<cmd>` literal. Deny rc is NOT swallowed: `os_process_spawn → OS_STATUS_DENIED` surfaces as a non-zero `$?` paired with the kernel-emitted `CAP:DENY:<sid>:app_exec:<resource>` marker. |
| `export VAR=...` (`SOSH_CAP_ENV_WRITE`)      | enforced (host-only no-op deny path per §3.b) — `sosh_cap_export`                  |

All §4 rows now route through `sosh_state_t.cap_check` before the
underlying `state->exec(...)` (or in-process equivalent) dispatch,
completing the §4 enumeration at `OS_ABI_VERSION = 0`. Embedder-side
wiring of `cap_check` to native `CAP_*` is tracked separately, as is
the `cap.deny` audit-ring follow-up (#389) covered by the
`audit_deny_recorded:sosh_audit_unwired_pending_issue_389` SKIP marker
on every sosh deny test.

Last verified against commit: 02514ac (sosh_cap_allow/deny wired via test.sh dispatcher); cat/ls FS_READ gate landed in sosh_cap_cat_ls; write/append FS_WRITE gate landed in sosh_cap_write_append; `export` ENV_WRITE gate landed in sosh_cap_export; `source` FS_READ + external-command APP_EXEC gates pinned by sosh_cap_source_exec (slice 3/3 of #351, refs #371) — §4 enforcement matrix is now complete at `OS_ABI_VERSION = 0`.
Last verified against commit: a43ef3d
