# Audit Marker Registry (v0 scaffold)

| Field | Value |
| --- | --- |
| Owner | Kernel capability + launcher/toolchain audit emitters (`kernel/cap/`, `kernel/user/`, `user/apps/cc/`) |
| Status | DRAFT (registry scaffold + first capability-mutation family population) |
| Tracks | [#587](https://github.com/rwrife/SecureOS/issues/587), [#635](https://github.com/rwrife/SecureOS/issues/635) |

## 1. Purpose and scope

This file is the **single index** of audit-marker families used by SecureOS.
It is intentionally a registry, not a replacement for each marker family's
source-of-truth contract.

Use this page to answer:

- which marker prefixes exist,
- who emits each family,
- who consumes/asserts each family,
- and where the normative contract lives.

Out of scope for this scaffold:

- redefining marker semantics already documented elsewhere,
- adding new marker families,
- changing kernel/user-space emission behavior.

## 2. Marker grammar (registry-level)

At the registry level, marker families are modeled as colon-delimited tokens:

- `DOMAIN:EVENT[:field[:field...]]`
- tokens are ASCII and case-sensitive unless the owning contract says otherwise,
- marker payload shape is owned by the family's authoritative document.

Emission surfaces:

- **Serial/log marker stream** — line-oriented markers consumed by QEMU/host
  harnesses (`TEST:*`, `CAP:*`, launcher/toolchain marker families).
- **In-memory audit ring / structured records** — kernel-maintained audit data
  where applicable (for example capability-check/grant/revoke records).

A family may have one or both surfaces; the authoritative contract specifies
which is normative.

## 3. Marker catalog

| marker_prefix | family | emitter | consumer / test surface | authoritative doc | gating issue |
| --- | --- | --- | --- | --- | --- |
| `CAP:DENY:<sid>:<cap>:<resource>` | Capability-deny marker | Capability gate deny paths (`kernel/cap/` + gated syscall/service paths) | Capability deny-path harnesses and serial-log assertions | [`capability-deny-contract.md`](capability-deny-contract.md) | [#164](https://github.com/rwrife/SecureOS/issues/164) |
| `CAP_AUDIT:...:op=GRANT:...` / `CAP_AUDIT:...:op=REVOKE:...` | Capability mutation audit markers (`cap.grant` / `cap.revoke` registry aliases) | Capability table mutation paths (`kernel/cap/capability.c`) + broker-mediated mutation paths (`kernel/cap/cap_broker.c`) | Capability audit fixture + broker share tests (`capability_audit_fixture`, `broker_share_*`) | [`capabilities.md`](capabilities.md), §4.1–§4.2 in this registry | [#635](https://github.com/rwrife/SecureOS/issues/635) |
| `AUTH_TYPE_UNSIGNED_BIN` (allow/deny/cached flow markers) | Unsigned local-binary authorization flow | Launcher unsigned-binary authorization path (`console_authorize_unsigned_binary`, `kernel/core/console.c`) | `console_unsigned_bin_auth_marker` (host-source literal pin) + `toolchain_unsigned_prompt_enforced` (runtime acceptance) | Marker grammar + target mapping locked in this registry row (issue #542) | [#410](https://github.com/rwrife/SecureOS/issues/410) |
| `launch.granted` / `launch.denied` (`owner_kind=...`) | Launcher execution-decision audit | Launcher execute/deny decision path (`kernel/user/launcher_exec.c`) | Launch allow/deny audit assertions (planned + existing launch tests) | Pending owner-kind contract tracked in [#554](https://github.com/rwrife/SecureOS/issues/554) | [#554](https://github.com/rwrife/SecureOS/issues/554) |
| `cc.compile.start` / `cc.compile.success` / `cc.compile.fail` | In-OS toolchain compile markers | In-OS `cc` driver (`user/apps/cc/`, planned M7 slices) | `tests/m7_toolchain/*` compile-path harnesses, including SKIP-pinned grammar contract target `toolchain_cc_audit_markers` (#571) and arena-exhaustion subtype pin `toolchain_cc_arena_exhaustion_audit_marker` (#610) | Base marker grammar contract in §3.1 below; arena-exhaustion subtype pin tracked in [#610](https://github.com/rwrife/SecureOS/issues/610) | [#571](https://github.com/rwrife/SecureOS/issues/571), [#610](https://github.com/rwrife/SecureOS/issues/610) |

### 3.1 `cc.compile.*` marker grammar (issue #571)

This subsection is the normative v0 grammar for compile-path audit markers.
The in-OS `cc` runtime implementation remains gated by
[#409](https://github.com/rwrife/SecureOS/issues/409) and
[#410](https://github.com/rwrife/SecureOS/issues/410), but once emitted,
markers MUST match these shapes exactly.

```text
cc.compile.start:<sid>:<input_path>:<arena_bytes>
cc.compile.success:<sid>:<input_path>:<output_sof_sha256>:<bytes>
cc.compile.fail:<sid>:<input_path>:<exit_code>:<reason_tag>
```

Field contract:

- `<sid>`: decimal launcher/session id for the compile invocation.
- `<input_path>`: canonical source path string passed to `cc` (for example
  `/apps/dev/hello.c`), no surrounding quotes.
- `<arena_bytes>`: decimal `runtime.arena_bytes` budget for this compile run.
- `<output_sof_sha256>`: lowercase 64-hex SHA-256 of the emitted SOF bytes.
- `<bytes>`: decimal byte length of the emitted SOF output.
- `<exit_code>`: numeric `cc` exit code from
  [`docs/in-os-toolchain/building-apps.md`](../in-os-toolchain/building-apps.md#cc-exit-codes-v0-contract-pin).
- `<reason_tag>`: stable, machine-parseable failure class (`usage_error`,
  `compile_error`, `link_error`, `io_error`, `arena_exhausted`,
  `internal_error`; additive future tags allowed only via docs + harness
  updates in lockstep).

Cross-contract requirements:

- `cc.compile.success` implies `exit_code=0` and a valid `<output_sof_sha256>`
  / `<bytes>` pair for the on-disk output artifact.
- `cc.compile.fail` implies non-zero `<exit_code>` and no success marker for
  the same compile unit.
- `reason_tag=arena_exhausted` joins with the capability-deny marker
  `CAP:DENY:<sid>:mem_brk:arena_bytes` (issue [#610](https://github.com/rwrife/SecureOS/issues/610)).

Harness ownership:

- `tests/m7_toolchain/toolchain_cc_audit_markers.sh` is the pre-#409/#410
  SKIP-pinned grammar contract harness for this section.
- Runtime execute harnesses (`toolchain_compiles_hello_in_os`,
  `toolchain_compile_error_reported`, `toolchain_cc_exit_codes_match_v0_table`,
  `toolchain_cc_arena_exhaustion_audit_marker`) consume these fields once the
  compiler driver slice lands.

## 4. Capability mutation family population (#635)

> `audit-markers.json` follow-up: #591 is still open, so this landing is
> intentionally **`.md`-only**. JSON row additions are deferred until #591's
> schema and validator are merged.

### 4.1 `cap.grant` (registry alias)

Canonical on-target serialized string (current implementation):

```text
CAP_AUDIT:seq=<n>:op=GRANT:actor=<actor_sid>:subject=<subject_sid>:cap=<capability_id>:result=<cap_result>:outcome=<audit_outcome>
```

Source-of-truth emit/format sites in `kernel/cap/` (line ranges as of this
file's verification stamp):

- [`kernel/cap/capability.c`](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/capability.c)
  - [`cap_grant_as_for_tests` (`L210-L243`)](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/capability.c#L210-L243) — admin-gated grant path.
  - [`cap_grant_for_tests` (`L183-L187`)](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/capability.c#L183-L187) — direct test-helper grant path.
  - [`cap_audit_format_event` (`L389-L414`)](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/capability.c#L389-L414) — canonical serialized line shape.
- [`kernel/cap/cap_broker.c`](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/cap_broker.c)
  - [`cap_broker_approve` (`L147-L177`)](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/cap_broker.c#L147-L177) — broker-mediated grant (`cap_audit_emit(CAP_AUDIT_OP_GRANT, ...)`).
  - [`cap_broker_deny` (`L179-L205`)](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/cap_broker.c#L179-L205) — denied broker grant request still records `op=GRANT` with deny-class `result`.

Pinned fields for the `cap.grant` family:

| Field | Encoded as | Contract |
| --- | --- | --- |
| `subject_sid` | `subject=<subject_sid>` | Subject whose capability set is mutated (not always equal to actor). |
| `capability` | `cap=<capability_id>` | Numeric `capability_id_t` value from `kernel/cap/capability.h`. |
| `resource` | `-` (not serialized in `CAP_AUDIT` v0) | Mutation events are table-level; no resource string is emitted in this family. Resource-specific deny telemetry stays in `CAP:DENY:*`. |
| `cause` | Derived from emitter callsite | Current in-tree causes: `cap_admin` (`cap_grant_as_for_tests`), `broker_share` (`cap_broker_approve` / `cap_broker_deny`), `self_test_helper` (`cap_grant_for_tests`). Future causes (e.g. `workflow_rule`, `revoke_subtree`) are additive and must be documented here when first emitted. |

### 4.2 `cap.revoke` (registry alias)

Canonical on-target serialized string (current implementation):

```text
CAP_AUDIT:seq=<n>:op=REVOKE:actor=<actor_sid>:subject=<subject_sid>:cap=<capability_id>:result=<cap_result>:outcome=<audit_outcome>
```

Source-of-truth emit/format sites in `kernel/cap/` (line ranges as of this
file's verification stamp):

- [`kernel/cap/capability.c`](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/capability.c)
  - [`cap_revoke_as_for_tests` (`L245-L265`)](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/capability.c#L245-L265) — admin-gated revoke path.
  - [`cap_revoke_for_tests` (`L189-L193`)](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/capability.c#L189-L193) — direct test-helper revoke path.
  - [`cap_audit_format_event` (`L389-L414`)](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/capability.c#L389-L414) — canonical serialized line shape.
- [`kernel/cap/cap_broker.c`](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/cap_broker.c)
  - [`cap_broker_revoke` (`L207-L235`)](https://github.com/rwrife/SecureOS/blob/6305aea4f0eaee1547ed0e4c2be022688af3c9cf/kernel/cap/cap_broker.c#L207-L235) — broker-owner or recipient self-revoke path.

Pinned fields for the `cap.revoke` family:

| Field | Encoded as | Contract |
| --- | --- | --- |
| `subject_sid` | `subject=<subject_sid>` | Subject whose capability is removed. |
| `capability` | `cap=<capability_id>` | Numeric `capability_id_t` value being revoked. |
| `resource` | `-` (not serialized in `CAP_AUDIT` v0) | Same v0 rule as `cap.grant`: no resource token in mutation lines. |
| `cause` | Derived from emitter callsite | Current in-tree causes: `cap_admin` (`cap_revoke_as_for_tests`), `broker_share` (`cap_broker_revoke`), `self_test_helper` (`cap_revoke_for_tests`). Reserved future cause: `revoke_subtree` (tracked by #241/#370; currently represented by the separate `CAP_AUDIT_OP_CASCADE_*` family, not by `op=REVOKE`). |

## 5. Lifecycle: adding or changing a marker family

When introducing or modifying an audit-marker family:

1. **Land/update the family contract first** in its owning ABI/process doc.
2. **Add/refresh the row in this registry** with emitter + consumer surfaces.
3. **Cross-link the gating issue/PR** so roadmap tracking can find it.
4. **Update test coverage** (or open/refresh the tracked harness issue).
5. **Bump `Last verified against commit`** in this file and any touched
   authoritative docs.

PR checklist (minimum):

- [ ] Marker-family authoritative contract exists (or is updated).
- [ ] Registry row added/updated in `docs/abi/audit-markers.md`.
- [ ] Consumer/test surface documented (or issue linked).
- [ ] `Last verified against commit` lines refreshed.

Last verified against commit: c50d044dd0
