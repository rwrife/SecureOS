# Capability Matrix Harness

Scope: BUILD_ROADMAP §6.1 (nightly matrix stage) and §6.2 (matrix dimensions).
Tracking issue: #151.

## Why

The validator bundle (`build/scripts/validate_bundle.sh` + the per-target
shells under `build/scripts/test_*.sh`) runs a single configuration per
target. As the M2 / M3 / M4 acceptance slices land (#92, #108, #115), each
adds *one* grant path and *one* deny path. Without a matrix runner, every
new slice multiplies the validator surface manually and §6.3 ("no merge on
failing capability regression tests") quietly degrades to "the one
combination we happened to test."

This harness is a thin orchestrator on top of the existing validator
targets — it does not introduce new test binaries. Cells are declared in
[`tests/matrix/capability_matrix.json`](../../tests/matrix/capability_matrix.json)
as `{cap_set, faux_policy, lifecycle_event}` triples plus the list of
existing `build/scripts/test.sh` targets each cell exercises.

## Dimensions (§6.2)

| Dimension          | Values today                              | Source of truth                              |
|--------------------|-------------------------------------------|----------------------------------------------|
| `cap_set`          | `minimal`, `typical`, (future: `full`)    | per-target manifest fixtures                 |
| `faux_policy`      | `off`, `on`                                | M3 fs_service substitution                   |
| `lifecycle_event`  | `none`, `owner_revoke`                    | M4/M5 broker revoke / owner-delete           |

Each cell is exposed to the underlying `test.sh` target via four
environment variables (`SECUREOS_MATRIX_CELL`, `SECUREOS_MATRIX_CAP_SET`,
`SECUREOS_MATRIX_FAUX_POLICY`, `SECUREOS_MATRIX_LIFECYCLE`) so that
individual targets can branch on them once they grow matrix-aware
fixtures. The initial cells re-use existing deterministic targets
unchanged; cells that the targets ignore today are still useful because
they will start to bite the moment a target reads its env var.

## Initial cells

See [`tests/matrix/capability_matrix.json`](../../tests/matrix/capability_matrix.json).

1. `minimal-faux_off-none` — today's default posture.
2. `minimal-faux_on-none` — M3 substitution probe (helloapp_deny +
   fs_service).
3. `typical-faux_off-owner_revoke` — M4/M5 lifecycle probe (cap_broker +
   capability_audit).

## Running

```bash
# Linux / macOS
build/scripts/test_matrix.sh                              # all cells
build/scripts/test_matrix.sh minimal-faux_off-none        # one cell
```

```powershell
# Windows
.\build\scripts\test_matrix.ps1
.\build\scripts\test_matrix.ps1 minimal-faux_off-none
```

Override the matrix file with `SECUREOS_MATRIX_FILE=path/to/matrix.json`.

## Artifacts

Each invocation creates `artifacts/runs/matrix-<run-id>/`:

- `matrix_report.json` — top-level summary (`schema:
  "secureos.matrix_report.v0"`).
- `<cell-id>/cell.json` — cell config snapshot.
- `<cell-id>/cell.log` — concatenated stdout/stderr per target.
- `<cell-id>/result.json` — per-cell `pass`/`fail` + per-target statuses.

The `matrix_report.json` shape mirrors the structure used by
`validator_report.json` (#110) so reviewers can diff regressions across
cells:

```json
{
  "schema": "secureos.matrix_report.v0",
  "run_id": "matrix-...",
  "generated_at": "...",
  "git_sha": "...",
  "matrix_file": ".../capability_matrix.json",
  "status": "pass",
  "cells": [
    {
      "id": "minimal-faux_off-none",
      "cap_set": "minimal",
      "faux_policy": "off",
      "lifecycle_event": "none",
      "status": "pass",
      "targets": { "cap_api_contract": "pass", "capability_gate": "pass" },
      "artifacts": ".../minimal-faux_off-none"
    }
  ]
}
```

## CI wiring

`pr-build.yml` runs the harness as a **non-blocking nightly job** initially
(`continue-on-error: true`) so it does not deepen the current PR-red
situation. Once the cells stabilize the maintainer can flip the stage to
required.

## Adding cells

1. Edit `tests/matrix/capability_matrix.json` — append a new object to the
   `cells` array with a unique `id`.
2. Pick the existing `test.sh` targets that exercise the new combination.
3. If the targets need new behavior, file a follow-up issue rather than
   muddying the harness — the matrix layer must stay thin.

## Dependencies / cross-references

- Builds on #110 (validator JSON report) — shares the `schema:` envelope
  convention.
- Coordinates with #92 / #108 / #115 (slice acceptance tests) — each new
  slice should land with at least one cell that exercises both its grant
  and deny path.
- Cross-platform parity tracked under #156; the `.sh` ↔ `.ps1` peers are
  expected to stay in lockstep.
