# 2026-05-16 Capability Grant/Deny Matrix Harness (BUILD_ROADMAP §6.1 nightly + §6.2)

Tracking issue: #151. Sibling pattern: #127 (M4 broker plan), #128 (M3 fs
plan), #121 (M5 ownership plan), #142 (M6 SDK plan). Composes on top of
#110 / PR #112 (machine-readable validator report) and #109 / PR #113
(test-plans registry).

## Goal

Carve a thin, dimension-driven test runner that iterates the three matrix
axes `BUILD_ROADMAP.md` §6.2 calls out (`cap_set`, `faux_policy`,
`lifecycle_event`) over a single declarative matrix file, emits one
per-cell artifact directory + JSON record, and rolls up a top-level
`matrix_report.json` reviewers can diff for regressions. Wire it as a
**non-blocking nightly** job first so it does not deepen the current
red-CI queue tracked by #126 / #139 / #147 / #152.

This plan is the test-design contract; execution lands behind a separate
*Execute* tracking issue once #92 / #108 / #115 acceptance tests stabilize
(per #151's "Dependencies" note).

## Scope

In:

- `tests/matrix/capability_matrix.yaml` — single source of truth for cells.
- `build/scripts/test_matrix.sh` (+ `.ps1` stub that delegates) — iterates
  cells, exports cell env to each child validator, writes per-cell
  artifacts, rolls up `matrix_report.json`.
- `docs/test-plans/matrix-report.schema.json` — JSON-Schema for the
  rollup, composing on top of #110's `validator-report.schema.json` (one
  embedded validator report per cell).
- `.github/workflows/nightly-matrix.yml` — separate workflow, schedule
  `cron: '0 7 * * *'` (UTC, ~midnight Pacific), `continue-on-error: true`,
  with the matrix report uploaded as an artifact. Does **not** gate PRs.
- An initial 3-cell matrix matching #151's "Done when":
  - `{cap_set=minimal, faux=off, lifecycle=none}` — today's default.
  - `{cap_set=minimal, faux=on,  lifecycle=none}` — M3 substitution probe.
  - `{cap_set=typical,faux=off, lifecycle=owner_revoke}` — M4/M5 lifecycle.

Out:

- Implementing per-slice acceptance tests themselves (covered by #92,
  #108, #115).
- Owner-cascade revocation semantics (waiting on #118 / M5).
- Cross-machine cells; faux-on for non-substitutable services.
- Promoting nightly → merge-blocking. That is a separate decision once
  the matrix has been green for ≥7 consecutive nightlies and #126's
  unblock chain (#104 → #107 → #125 → #134) has cleared.

## Layering against in-flight work

- **#110 / PR #112** already defines `validator_report.json` with a
  per-target `status ∈ {pass,fail,harness_error}` enum and
  `summary{total,passed,failed,harnessErrors}`. The matrix harness invokes
  `build/scripts/validate_bundle.sh` per cell with cell env injected, so
  each cell's per-target output is a normal validator report. The matrix
  rollup is a thin wrapper: `cells[].{cellId, axes, status, reportPath,
  summary}` + a top-level `summary{cellsTotal, cellsPassed, cellsFailed,
  cellsHarnessError}`. No change to the existing validator schema.
- **#109 / PR #113** defines `docs/test-plans/m0-m1-plan.yaml` with the
  task registry. Each matrix cell may carry an optional `tasks: [<id>...]`
  field referencing registered task ids; the harness does not enforce it
  yet but the schema permits it so a future change can cross-link cells
  to registry rows.
- **#101 / PR #104** (`tools/sof_wrap` +x discipline) and **#90 / PR #95
  / PR #114** (validator script +x discipline) — the matrix harness
  shells out to `build/scripts/validate_bundle.sh` and so inherits those
  fixes automatically; it adds nothing new to the +x surface.

## Matrix file format

`tests/matrix/capability_matrix.yaml`:

```yaml
version: 1
cells:
  - id: minimal-faux-off-none
    cap_set: minimal
    faux_policy: off
    lifecycle_event: none
    tasks: []           # optional, refs docs/test-plans/m0-m1-plan.yaml
    targets: default    # default = the existing validate_bundle TEST_TARGETS
  - id: minimal-faux-on-none
    cap_set: minimal
    faux_policy: on
    lifecycle_event: none
    targets: default
  - id: typical-faux-off-owner-revoke
    cap_set: typical
    faux_policy: off
    lifecycle_event: owner_revoke
    targets: default
```

Axis enumeration (frozen by this plan; new axes require a follow-up plan
to keep cell-count growth visible):

- `cap_set ∈ {minimal, typical, full}` — `minimal` ≈ today's M0/M1
  baseline (CAP_BOOT + CAP_LOG only); `typical` adds CAP_CONSOLE_WRITE +
  CAP_FS_READ + CAP_FS_WRITE; `full` adds the M4 broker caps. Each set is
  defined as a constant header (`tests/matrix/cap_sets.h` —
  execution-time deliverable, not this plan) so the kernel boot path can
  read it deterministically.
- `faux_policy ∈ {off, on}` — when `on`, fs_service responds from the
  ephemeral substitution path (per #88 / PR #88's faux mode); when `off`,
  persistent disk only.
- `lifecycle_event ∈ {none, owner_revoke, owner_delete}` — `none` is the
  baseline; `owner_revoke` mid-run sends a `cap_broker_revoke(OWNER,
  share_id)` (relies on #99); `owner_delete` waits on #118 / M5 ownership
  graph and is **not** in the initial cells.

## Per-cell artifact layout

```
artifacts/runs/matrix-<run_id>/
  matrix_report.json                # rollup (this slice's schema)
  cells/
    minimal-faux-off-none/
      cell.json                     # axis values + env injected
      validator_report.json         # the #110 schema
      qemu/                         # per-target serial logs
      tests/                        # per-target test stdout
    minimal-faux-on-none/
      ...
    typical-faux-off-owner-revoke/
      ...
```

The harness sets `SECUREOS_RUN_ID=matrix-<run_id>/cells/<cellId>` before
each call to `validate_bundle.sh` so the existing run-dir layout slots
under the matrix root with no validator-side change.

## Cell env contract

Cells are exposed to validators as environment variables, **not** by
patching scripts. The kernel / fs_service / broker test paths read them
during boot:

| Axis | Env var | Values |
|---|---|---|
| `cap_set` | `SECUREOS_MATRIX_CAP_SET` | `minimal` \| `typical` \| `full` |
| `faux_policy` | `SECUREOS_MATRIX_FAUX_POLICY` | `off` \| `on` |
| `lifecycle_event` | `SECUREOS_MATRIX_LIFECYCLE` | `none` \| `owner_revoke` \| `owner_delete` |

A boot-time check in the matrix-aware tests reads each var, applies the
cell config, and emits `MATRIX:CELL:<id>:<axis>=<value>` markers into the
serial log so cell attribution is visible in QEMU logs.

If a validator does not consume the env, it runs in its default mode and
the cell still produces a parseable record. That is by design: rolling
existing targets into the matrix should not require changing them.

## Rollup schema

`docs/test-plans/matrix-report.schema.json` (draft 2020-12, mirrors the
shape of #110's schema):

```jsonc
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "additionalProperties": false,
  "required": ["matrixRunId", "startedAt", "finishedAt", "git", "summary", "cells"],
  "properties": {
    "matrixRunId": { "type": "string" },
    "startedAt":   { "type": "string", "format": "date-time" },
    "finishedAt":  { "type": "string", "format": "date-time" },
    "git": {
      "type": "object",
      "required": ["sha"],
      "properties": { "sha": { "type": "string" }, "ref": { "type": "string" } },
      "additionalProperties": false
    },
    "summary": {
      "type": "object",
      "required": ["cellsTotal", "cellsPassed", "cellsFailed", "cellsHarnessError"],
      "properties": {
        "cellsTotal":         { "type": "integer", "minimum": 0 },
        "cellsPassed":        { "type": "integer", "minimum": 0 },
        "cellsFailed":        { "type": "integer", "minimum": 0 },
        "cellsHarnessError":  { "type": "integer", "minimum": 0 }
      },
      "additionalProperties": false
    },
    "cells": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["cellId", "axes", "status", "reportPath"],
        "properties": {
          "cellId": { "type": "string" },
          "axes": {
            "type": "object",
            "required": ["cap_set", "faux_policy", "lifecycle_event"],
            "properties": {
              "cap_set":         { "enum": ["minimal", "typical", "full"] },
              "faux_policy":     { "enum": ["off", "on"] },
              "lifecycle_event": { "enum": ["none", "owner_revoke", "owner_delete"] }
            },
            "additionalProperties": false
          },
          "status":     { "enum": ["pass", "fail", "harness_error"] },
          "reportPath": { "type": "string" },
          "summary":    { "$ref": "#/$defs/validatorSummary" },
          "tasks":      { "type": "array", "items": { "type": "string" } }
        },
        "additionalProperties": false
      }
    }
  },
  "$defs": {
    "validatorSummary": {
      "type": "object",
      "required": ["total", "passed", "failed", "harnessErrors"],
      "additionalProperties": false,
      "properties": {
        "total":         { "type": "integer", "minimum": 0 },
        "passed":        { "type": "integer", "minimum": 0 },
        "failed":        { "type": "integer", "minimum": 0 },
        "harnessErrors": { "type": "integer", "minimum": 0 }
      }
    }
  }
}
```

A cell's `status` is the rollup of its embedded validator report:

- `harness_error` if any target inside the cell reported
  `harness_error` (kept distinct from real failures per #110's whole
  point).
- `fail` if no harness errors but any target failed.
- `pass` otherwise.

## Wire-up

- `build/scripts/test_matrix.sh` — new shell, +x. Iterates
  `tests/matrix/capability_matrix.yaml`, exports cell env, invokes
  `validate_bundle.sh` per cell, writes `cells/<id>/cell.json` and rolls
  up. Validates `matrix_report.json` against the schema before exit
  (mirroring #110 / PR #112's discipline).
- `build/scripts/test_matrix.ps1` — delegates to the `.sh` inside the
  toolchain container, same pattern as `build_bearssl.ps1` /
  `validate_bundle.ps1`.
- `build/scripts/test.sh` — new `matrix` dispatch arm runs a smoke pass
  (single `minimal-faux-off-none` cell) so a contributor can sanity-check
  the harness locally without paying the full matrix cost.
- `.github/workflows/nightly-matrix.yml` — separate workflow on
  `schedule: cron: '0 7 * * *'` UTC. Uses the same `+x` defense step PR
  #114 adds to `pr-build.yml`. Uploads `artifacts/runs/matrix-*/` as a
  workflow artifact. `continue-on-error: true` on the matrix step so a
  red nightly does not page anyone until we flip it to blocking.
- Not added to `validate_bundle.sh` `TEST_TARGETS`. The matrix is a
  *driver of* validate_bundle, not a target inside it. Adding it to
  `TEST_TARGETS` would create an infinite-recursion bug and conflate
  per-PR cost with nightly cost. The single-cell `matrix` smoke target
  on `test.sh` is the cheap PR-time signal that the harness itself
  parses; the full N-cell run is nightly only.

## Acceptance tests (against this plan, not the slice)

Three deterministic per-cell asserts the matrix harness self-tests on
its own smoke run (mirrors the structured-marker discipline of the M2/M3
plans):

- `matrix_smoke_single_cell` — `test_matrix.sh --smoke` runs exactly one
  cell, produces a valid `matrix_report.json` with
  `summary.cellsTotal=1`, exits 0 on green.
- `matrix_schema_rejects_bad_axis` — fixture matrix with
  `cap_set: bogus` is rejected at load time with
  `TEST:FAIL:matrix:invalid_axis_value` (not a silent pass).
- `matrix_rollup_attributes_harness_error` — fixture cell that points
  at a missing validator script must surface as
  `status: harness_error` at the cell level (not `fail`), so #110's
  "harness vs real-failure" distinction propagates upward.

## Cross-references

- #110 / PR #112 — validator report schema (this slice's per-cell payload).
- #109 / PR #113 — test-plans registry (optional `tasks[]` cross-link).
- #92 — M2 HelloApp allow + deny console-write (first slice the matrix
  will meaningfully exercise once it lands; deny-path cell is the
  natural follow-up).
- #108 — M3 FS deny + ephemeral-reset (drives `faux_policy=on` cell).
- #115 / #127 — M4 broker allow / deny / revoke (drives
  `lifecycle_event=owner_revoke` cell).
- #118 / PR #121 — M5 ownership graph (unlocks
  `lifecycle_event=owner_delete` cell as a follow-up).
- #136 / PR #142 — M6 SDK (external apps become first-class matrix
  subjects once the SDK lands).
- #126 / #139 / #147 / #152 — merge-queue hold; this plan does **not**
  deepen the queue (docs-only).

## Done-when mapping (#151)

- [x] Plan file at `plans/2026-05-16-...` matching the recent plan shape.
- [x] Names `build/scripts/test_matrix.sh` (+ `.ps1`) and the YAML matrix file.
- [x] Lists the 3 initial cells per the issue body.
- [x] Per-cell artifact directory layout specified, with per-cell config
      + serial log + pass/fail JSON.
- [x] Top-level rollup file (`matrix_report.json`) + schema specified.
- [x] Documents non-blocking nightly CI wiring so the harness does not
      deepen the current red-CI situation.
- [x] Documents where docs live (this `plans/` file now; lifts into
      `docs/test-plans/` once #109 / PR #113 lands the index page).
- [ ] *Deferred*: opening the *Execute plan: capability matrix harness*
      tracking issue. Will be filed in a follow-up planning run after
      #112 (validator schema) and at least one of #100 / #128 / #127
      slice executions land, so the cell contents the matrix iterates
      are real, not aspirational.

## Out of scope

- Per-cell acceptance test implementations (covered by their own slice
  plans).
- Promoting the nightly workflow to a PR-blocking gate.
- Re-keying any existing target to consume the matrix env directly;
  targets remain matrix-agnostic in this slice.
- Mirroring into a hosted dashboard / Grafana / etc.; `matrix_report.json`
  is the canonical artifact and external rendering is downstream.
