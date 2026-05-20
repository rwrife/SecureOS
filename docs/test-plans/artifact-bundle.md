# Per-run artifact bundle layout (`artifacts/runs/<id>/`)

Status: draft (issue #161, BUILD_ROADMAP §4.4)

This document defines the on-disk layout used for **per-run** build / test /
QEMU artifacts. The goal is to capture everything needed to replay or
postmortem a single run into one self-contained directory, so:

- agents can diff two runs by `diff -ru artifacts/runs/A artifacts/runs/B`,
- the validator JSON (#110) has a canonical home next to the logs/image it
  describes,
- the CI matrix harness (#151) can attach one bundle per matrix cell.

## Run id

```
<id> ::= <UTC-timestamp>-<short-sha>[ -<kind> ]
<UTC-timestamp> ::= YYYYMMDDThhmmssZ        (date -u +%Y%m%dT%H%M%SZ)
<short-sha>     ::= git rev-parse --short HEAD
<kind>          ::= optional, lowercase, e.g. "matrix-cap-deny-net"
```

The id is chosen by the **top-level driver** (typically `validate_bundle.sh`)
and propagated to child scripts via the `SECUREOS_RUN_ID` environment
variable. Child scripts that observe `SECUREOS_RUN_ID` MUST reuse it and
MUST NOT mint a new one — that is what keeps a single run's artifacts
co-located.

If `SECUREOS_RUN_ID` is unset, a script writing into the bundle is allowed
to mint one (using the rule above) so it remains useful when invoked
standalone.

## Layout

```
artifacts/runs/<id>/
├── build_metadata.json          # run id, timestamps, git sha/ref, paths
├── validator_report.json        # validator summary (schemaVersion=1, #110)
├── secureos.iso                 # snapshot of the kernel ISO (optional)
├── qemu/
│   ├── <test>.log               # raw serial / stdout+stderr capture
│   ├── <test>.meta.json         # per-invocation metadata (see below)
│   └── run.json                 # aggregate manifest of qemu invocations in this run
└── tests/
    └── <test>_*.json            # per-test summary artifacts (e.g. capability_audit_summary.json)
```

Notes:
- All paths inside `validator_report.json` are **relative to the bundle
  root** (`artifacts/runs/<id>/`) so a bundle can be moved or uploaded
  whole without rewriting paths.
- `qemu/<test>.log` is the same content currently written to
  `artifacts/qemu/<test>.log` (the flat path is preserved for backward
  compatibility; the run-scoped copy is the canonical replay surface).

### `qemu/run.json`

A small manifest describing every QEMU invocation that belongs to this
run (today: one entry per call to `run_qemu.sh --test <name>`).

```json
{
  "schemaVersion": 1,
  "runId": "20260517T141800Z-abc1234",
  "invocations": [
    {
      "test": "hello_boot",
      "logFile": "qemu/hello_boot.log",
      "metaFile": "qemu/hello_boot.meta.json",
      "imageSha256": "…",
      "command": ["qemu-system-x86_64", "…"],
      "exitCode": 33,
      "wallClockSeconds": 4,
      "markers": { "start": true, "pass": true, "fail": false },
      "status": "pass"
    }
  ]
}
```

`run.json` is **append-only within a run**: each `run_qemu.sh` invocation
adds one element to `invocations`. Concurrent invocations within one run
id are out of scope for the initial implementation (BUILD_ROADMAP §6.1
matrix harness will revisit if needed).

### `build_metadata.json`

Written by `validate_bundle.sh`. Minimum fields:

```json
{
  "runId": "…",
  "startedAt": "2026-05-17T14:18:00Z",
  "finishedAt": "2026-05-17T14:21:33Z",
  "git": { "sha": "…", "ref": "main" },
  "artifactsRoot": "/abs/path/to/artifacts/runs/<id>"
}
```

### `validator_report.json`

Owned by `validate_bundle.sh` (#110). This document does not re-spec the
report — it only fixes its **location** as `artifacts/runs/<id>/validator_report.json`
so #110 and #161 agree.

## Cross-platform parity

`build/scripts/run_qemu.ps1` and `build/scripts/validate_bundle.ps1`
shell out to their `.sh` peers inside the toolchain container, so the
on-disk layout is produced by exactly one implementation and is
identical across Linux and Windows hosts. This preserves the AGENTS.md
sh ↔ ps1 parity rule (#156) without duplicating the layout logic.

## Out of scope (follow-ups)

- Bundle retention / pruning policy.
- Uploading bundles as CI artifacts.
- Concurrent multi-process writers to a single `qemu/run.json`.
- A worked example bundle checked into the repo — once #110 + #161 are
  both live, capture one canonical bundle under
  `docs/test-plans/examples/run-sample/` as documentation.

## Related

- BUILD_ROADMAP.md §4.4 (artifact policy)
- #110 (validator JSON report)
- #151 (CI matrix harness)
- #156 (sh ↔ ps1 parity)
- #109 / PR #113 (test-plans registry — this file lives next to that)
