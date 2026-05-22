# Capability Registry

Status: DRAFT (issue #234 — single machine-readable index of every `CAP_*`)
Owners: capability subsystem (`kernel/cap/`), validators (`build/scripts/`).

## Why this exists

The capability set is now scattered across several authoritative locations:

- the C enum in `kernel/cap/capability.h`,
- the human-readable ABI table in [`capabilities.md`](capabilities.md),
- the deny-marker grammar in [`capability-deny-contract.md`](capability-deny-contract.md),
- the deny-marker name table in `kernel/cap/cap_deny_marker.c`,
- per-cap allow/deny test scripts under `build/scripts/test_*.sh`,
- the capability-matrix harness fixture in `tests/matrix/capability_matrix.json`,
- the plans under `plans/` that introduced each capability.

This registry is a single index that an agent (or curious human) can
consult to answer *for any given `CAP_*`*: which subject kinds may hold it,
what its `CAP:DENY:` marker looks like, which `build/scripts/test.sh`
targets exercise its allow/deny paths, which plan owns it, and the
`OS_ABI_VERSION` at which its numeric id was frozen.

Per BUILD_ROADMAP.md §4 (Agentic Build/Test System) and §7 (ABI and
Interface Freeze Plan), the registry MUST be machine-checkable: when a new
`CAP_*` lands in `kernel/cap/capability.h`, the validator described below
fails CI unless the same id is registered here in the same change.

## Files

- [`capability-registry.json`](capability-registry.json) — machine-readable
  source of truth. Schema is intentionally inline (no separate JSON Schema
  file yet); structure is enforced by the validator.
- This file — human-readable mirror, plus the validator contract.

## Rows

Each row in `capability-registry.json` has the following fields. All are
required unless explicitly marked optional:

| Field               | Type           | Meaning |
| ------------------- | -------------- | ------- |
| `cap_id`            | string         | Symbolic name, exactly as declared in `kernel/cap/capability.h` (`CAP_*`). |
| `numeric_id`        | integer        | The frozen numeric value of the enum. Append-only — never renumbered. Cross-checked against [`capabilities.md`](capabilities.md). |
| `subject_kinds`     | array<string>  | Human-readable list of which subject kinds may legally hold this capability today (e.g. `app`, `launcher`, `bootstrap_root`, `sealed_build_only`). Informational; not enforced by the kernel. |
| `deny_marker`       | string         | The canonical `CAP:DENY:<actor>:<lowercase_name>:<resource>` shape for this capability. Must conform to the grammar in [`capability-deny-contract.md`](capability-deny-contract.md) §4. Placeholders `<actor_subject_id>` and `<resource>` (or a service-specific stand-in like `<path>`, `<topic>`, `<scheme_host>`, `<app_name>`) are allowed in the registry text — at runtime the formatter in `kernel/cap/cap_deny_marker.c` substitutes the real values. |
| `allow_test_target` | string \| null | `build/scripts/test.sh` target name that exercises a representative grant path. `null` when no per-cap allow target exists today (e.g. `CAP_SYSCALL` — the M1 stub denies everything by design). |
| `deny_test_target`  | string \| null | `build/scripts/test.sh` target name that exercises a representative explicit-deny path. `null` only when justified inline; aim is to have one for every capability. |
| `owning_plan`       | string \| null | Path (relative to repo root) to the plan under `plans/` that introduced or governs this capability. `null` when no single plan owns it (typically the very-early caps that predate the plan-per-slice convention). |
| `frozen_since_abi`  | string         | The `OS_ABI_VERSION` (formatted `"<major>.<minor>"`, see [`versioning.md`](versioning.md)) at which this `cap_id`'s numeric value was frozen. All current rows are `"0.0"` because every cap predates the first freeze. |

## Validator

`build/scripts/validate_capability_registry.sh` (and its PowerShell peer
`validate_capability_registry.ps1`, per the #156 parity rule) enforces:

1. Every `CAP_*` identifier declared in `kernel/cap/capability.h`'s
   `capability_id_t` enum appears **exactly once** in
   `capability-registry.json`.
2. Every `capability-registry.json` row's `cap_id` is declared in
   `kernel/cap/capability.h`'s `capability_id_t` enum (no orphans).
3. Every `capability-registry.json` row's `numeric_id` matches the value
   declared in `kernel/cap/capability.h`.
4. Every non-null `allow_test_target` / `deny_test_target` is a target
   recognized by `build/scripts/test.sh` (matched against the `case`
   dispatch labels in `test.sh`).
5. Every `deny_marker` parses against the marker prefix and field grammar
   from [`capability-deny-contract.md`](capability-deny-contract.md) §4
   (i.e. starts with `CAP:DENY:`, has the right number of colon-delimited
   fields, and the capability-name segment is lowercase and matches the
   `cap_id` with the `CAP_` prefix stripped).
6. Every non-null `owning_plan` resolves to a file under `plans/`.

Exit codes:

- `0` — every check passed.
- `1` — at least one check failed (one `REGISTRY_VALIDATE:FAIL:<reason>`
  marker per failure on stderr).
- `2` — environment error (missing `capability.h`, missing
  `capability-registry.json`, malformed JSON).

The validator emits stable, grep-able markers so the lint stage can
classify failures:

```
REGISTRY_VALIDATE:START
REGISTRY_VALIDATE:PASS:<check>
REGISTRY_VALIDATE:FAIL:<check>:<reason>
REGISTRY_VALIDATE:DONE
```

## CI wiring

The validator is invoked from `build/scripts/lint.sh` as the
`registry` check, behind the `LINT_REGISTRY_FATAL` environment variable
(default `0` — warn-only) so the first run on `main` cannot accidentally
red-gate unrelated PRs. Per the determinism-check rollout pattern from
issue #176, the flip to `1` (blocking) is tracked as a separate follow-up
issue filed at the same time this registry lands. Until that flip the
lint stage prints `TEST:PASS:lint_capability_registry:warn_only` even on
failure (with a `TEST:WARN:` line carrying the specific failure reason).

## Negative canary

`tests/integration/_canary_must_fail/capability_registry_drift.sh` adds a
fake `CAP_*` symbol to a sandboxed copy of `capability.h` and asserts the
validator emits `REGISTRY_VALIDATE:FAIL:enum_not_in_registry`. This
mirrors the canary discipline introduced in #213 — it proves that the
validator actually catches the regression it claims to catch, not that it
silently no-ops.

## How to add a new capability

1. Append a new entry to the `capability_id_t` enum in
   `kernel/cap/capability.h` (next free numeric id; never renumber).
2. Add a row in `kernel/cap/cap_deny_marker.c`'s `cdm_cap_names[]` so the
   shared formatter can render the new capability's deny marker.
3. Add a row in `docs/abi/capability-registry.json` with all required
   fields.
4. Update [`capabilities.md`](capabilities.md) so the human-readable ABI
   table stays in sync.
5. Run `build/scripts/validate_capability_registry.sh` locally — it must
   exit 0 before the change is mergeable.

## Provenance

Last verified against commit: (regenerated on each touch — see provenance
note in [`README.md`](README.md))
