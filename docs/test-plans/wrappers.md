# Agent Tool Wrapper Contract (`os-*`)

Issue: [#162](https://github.com/rwrife/SecureOS/issues/162) ·
Roadmap: `BUILD_ROADMAP.md` §4.3 ("Deterministic tool API wrappers")

## Why

Agents (and the cron sessions that drive them) should not invoke raw
`build/scripts/*.sh` directly. Each raw script has its own argv shape,
exit-code semantics, and (mostly) no machine-readable output. The
`os-*` wrappers stabilize that surface so an agent can drive any
combination of build/test steps with one parsing rule and one place to
discover artifact paths.

## Wrapper set

| Wrapper       | Wraps                              | Primary artifacts                                      |
|---------------|------------------------------------|--------------------------------------------------------|
| `os-build`    | `build.sh <target>`                | `artifacts/iso/`, `artifacts/kernel/`, `artifacts/disk/` |
| `os-package`  | `build_disk_image.sh`              | `artifacts/disk/secureos-disk.img`                     |
| `os-run-qemu` | `run_qemu.sh --test <name>`        | `artifacts/qemu/<name>.log`, `artifacts/qemu/<name>.meta.json` |
| `os-validate` | `validate_bundle.sh`               | `artifacts/runs/<run_id>/validator_report.json`, `build_metadata.json` |
| `os-snapshot` | tars `artifacts/runs/<run_id>/`    | `artifacts/snapshots/<run_id>.tar.gz`                  |

PowerShell peers (`os-build.ps1`, etc.) live next to each wrapper and
delegate to the `.sh` peer inside the pinned toolchain container, so
the JSON envelope is produced by a single implementation across
Linux / macOS / Windows. This preserves the `.sh ↔ .ps1` parity rule
(see [#156](https://github.com/rwrife/SecureOS/issues/156)).

## JSON envelope (v1)

Every wrapper writes the same envelope to stdout AND mirrors it into
the per-run bundle at `artifacts/runs/<run_id>/<tool>.json`:

```json
{
  "tool":        "os-build",
  "version":     "1",
  "ok":          true,
  "exit_code":   0,
  "artifacts":   ["artifacts/iso", "artifacts/kernel", "artifacts/disk"],
  "started_at":  "2026-05-17T17:30:00Z",
  "finished_at": "2026-05-17T17:31:42Z",
  "run_id":      "20260517T173000Z-abc1234"
}
```

On failure the envelope also includes a `"reason"` string and `ok` is
`false`. Exit codes from the underlying script are preserved.

## `run_id` selection

The wrappers reuse the `SECUREOS_RUN_ID` contract from the per-run
bundle layout
([#161](https://github.com/rwrife/SecureOS/issues/161) /
`docs/test-plans/artifact-bundle.md` when present):

1. If `SECUREOS_RUN_ID` is set in the environment, use it verbatim.
2. Otherwise derive `<UTC timestamp>-<short git sha>`
   (e.g. `20260517T173000Z-abc1234`).

`os-validate` exports `SECUREOS_RUN_ID` before invoking
`validate_bundle.sh` so that the wrapper envelope, the validator's
own structured report (#110), and any sibling `os-run-qemu` calls all
land in the same `artifacts/runs/<run_id>/` bundle. The matrix
harness ([#151](https://github.com/rwrife/SecureOS/issues/151)) can
set `SECUREOS_RUN_ID=<run>-<cell>` per cell and get exactly one
bundle per matrix cell with no extra plumbing.

## Usage pattern (for agents)

```bash
export SECUREOS_RUN_ID="cron-$(date -u +%Y%m%dT%H%M%SZ)"

build/scripts/os-build image      | tee /tmp/os-build.json
build/scripts/os-package          | tee /tmp/os-package.json
build/scripts/os-run-qemu --test kernel_console | tee /tmp/os-run-qemu.json
build/scripts/os-validate         | tee /tmp/os-validate.json
build/scripts/os-snapshot         | tee /tmp/os-snapshot.json

# All five JSON envelopes are also under:
#   artifacts/runs/$SECUREOS_RUN_ID/{os-build,os-package,os-run-qemu,os-validate,os-snapshot}.json
```

Agents should parse `ok` first, then read `artifacts[]` and
`run_id` to locate logs. Treat unknown fields as forward-compatible.

## Coordination

- **#110** `os-validate` does not respec the validator report schema;
  the validator continues to own `validator_report.json`. The wrapper
  envelope is additive metadata about *invocation success*, not the
  validation result content.
- **#151** Each matrix cell gets its own `SECUREOS_RUN_ID`, hence its
  own bundle and its own set of envelopes.
- **#161** Per-run bundle layout is the single home for both the
  envelopes and the underlying tools' detailed outputs.
- **#156** Parity is preserved by construction: the `.ps1` peers shell
  into the container and run the `.sh` peer.
- **#136 (M6 SDK)** The same wrapper set is the natural seed for the
  public `os-cc` / `os-pack` / `os-run` family, so freezing this
  contract now keeps the SDK surface stable.

## Out of scope

- Rewriting the underlying scripts (separate hardening passes per
  #91 / #129 / #140).
- Publishing wrappers as a `bin/` on `$PATH` for end users (M6 / #136).
- Bundle signing or upload (M6 + ADR follow-up).
