# docs/test-plans/

This directory is the human-readable index of SecureOS milestone tasks, their
execution status, and the schema used to encode them.

It exists to answer one question without scraping `gh issue list` or `git log`:

> "Which CAP-* / M-* tasks are done, which are in flight, and where is the
> closing artifact?"

## Files

- [`task-schema.md`](task-schema.md) — short walk-through of
  [`manifests/task-dag.schema.json`](../../manifests/task-dag.schema.json),
  the normative schema for the task DAG used by validators and agents.
- [`m0-m1-plan.yaml`](m0-m1-plan.yaml) — milestone/task registry for M0, M1,
  and the in-flight M2 slice. Validates against
  [`manifests/task-dag.schema.json`](../../manifests/task-dag.schema.json).
- [`validator-report.schema.json`](validator-report.schema.json) — schema for
  the JSON validator report emitted by `build/scripts/validate_bundle.sh` (see
  issue #110 / PR #112).

## Update cadence

Update [`m0-m1-plan.yaml`](m0-m1-plan.yaml):

1. **When a CAP-\* / M-\* slice closes** — flip its `artifacts[]` `status:`
   tag from `status:in_progress` / `status:pending` to `status:done` and add
   the closing PR reference (e.g. `pr:#46`).
2. **At the end of each milestone** — verify all member tasks are `done`,
   bump the milestone summary in this README if needed, and roll the
   `entryTasks` of the next milestone into `pipeline.entryTasks` (or create a
   `mN-plan.yaml` sibling and update this index).
3. **When the upstream schema changes** — re-run validation locally:

```bash
python3 - <<'PY'
import json, yaml, sys
try:
    import jsonschema
except ImportError:
    sys.exit("pip install jsonschema pyyaml")
schema = json.load(open("manifests/task-dag.schema.json"))
plan   = yaml.safe_load(open("docs/test-plans/m0-m1-plan.yaml"))
jsonschema.validate(plan, schema)
print("OK")
PY
```

A CI step that runs the same validation is listed as an optional follow-up
in issue #109 — out of scope for the initial registry.

## Conventions

The DAG schema (`task-dag.schema.json`) is execution-shaped: every task has a
`run` command and a `passCondition`. The registry uses the same shape so it
validates, with two documentation conventions layered on top:

- **Status tag** — each task carries an `artifacts[]` entry of the form
  `status:done`, `status:in_progress`, or `status:pending`.
- **Closure reference** — closed tasks also carry one or more
  `pr:#<num>` and/or `issue:#<num>` `artifacts[]` entries pointing at the
  merged PR and originating issue. Open tasks may carry an `issue:#<num>`
  entry only.

See [`task-schema.md`](task-schema.md) for an example.

## Milestone summary (as of 2026-05-12)

- **M0 — Tooling & boot smoke** — `done`. Phase-0 toolchain, build/test
  wrappers, deterministic QEMU harness, COM1 + VGA writers, isa-debug-exit
  signaling, `hello_boot` target with marker parser, and the negative-path
  fixture all merged (#1–#33).
- **M1 — Capability core (CAP-001 .. CAP-020)** — `done`. All twenty CAP-\*
  tasks merged via PRs #35, #36, #38, #41, #42, #44, #46, #49, #52, #54, #59,
  #61, #63, #65, #67, #69, #71, #73, #75, and #76's closing PR.
- **M2 — Console service + launcher + HelloApp** — `in_progress`. Tracking
  issues: #82 (slice plan), #92 + #100 (deny-path acceptance test), #93 / #97
  (ABI reference).

## Related

- [`BUILD_ROADMAP.md`](../../BUILD_ROADMAP.md) — §8 items 11 & 12 motivate
  this directory.
- [`manifests/task-dag.example.json`](../../manifests/task-dag.example.json)
  — minimal example of the DAG shape.
- [`docs/abi/`](../abi/) — ABI reference (issue #93).
