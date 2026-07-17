# Audit Marker Registry (v0 scaffold)

| Field | Value |
| --- | --- |
| Owner | Kernel capability + launcher/toolchain audit emitters (`kernel/cap/`, `kernel/user/`, `user/apps/cc/`) |
| Status | DRAFT (registry scaffold only; family-specific contracts remain authoritative in their owning docs/issues) |
| Tracks | [#587](https://github.com/rwrife/SecureOS/issues/587) |

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
| `AUTH_TYPE_UNSIGNED_BIN` (allow/deny/cached flow markers) | Unsigned local-binary authorization flow | Launcher unsigned-binary authorization path (M7/M6 trust model) | Unsigned-run prompt/decision harnesses (planned) | Pending contract landing tracked in [#542](https://github.com/rwrife/SecureOS/issues/542) | [#542](https://github.com/rwrife/SecureOS/issues/542) |
| `launch.granted` / `launch.denied` (`owner_kind=...`) | Launcher execution-decision audit | Launcher execute/deny decision path (`kernel/user/launcher_exec.c`) | Launch allow/deny audit assertions (planned + existing launch tests) | Pending owner-kind contract tracked in [#554](https://github.com/rwrife/SecureOS/issues/554) | [#554](https://github.com/rwrife/SecureOS/issues/554) |
| `cc.compile.start` / `cc.compile.success` / `cc.compile.fail` | In-OS toolchain compile markers | In-OS `cc` driver (`user/apps/cc/`, planned M7 slices) | `tests/m7_toolchain/*` compile-path harnesses (planned) | Pending contract tracked in [#571](https://github.com/rwrife/SecureOS/issues/571) | [#571](https://github.com/rwrife/SecureOS/issues/571) |

## 4. Lifecycle: adding or changing a marker family

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

Last verified against commit: e7aea50a277b0a49aab460d5cd4a1cdcb6dae6b3
