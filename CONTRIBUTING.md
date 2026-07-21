# Contributing to SecureOS

Thanks for contributing to SecureOS.

This guide covers:
- Local setup requirements
- How to build the project
- How to run the SecureOS console in headless and graphical modes

## Prerequisites

Required for all contributors:
- Docker
- Git

Required for graphical mode only:
- Host QEMU binary on PATH: `qemu-system-x86_64` (or `qemu-system-x86_64.exe` on Windows)

Recommended:
- At least 4 GB free disk space for images and artifacts

## Clone and Enter the Repo

```bash
git clone https://github.com/rwrife/SecureOS.git
cd SecureOS
```

## Setup

### Windows

No extra bootstrap script is required.

Verify Docker is available:

```powershell
docker --version
```

### macOS

Run the bootstrap helper:

```bash
./scripts/setup-macos.sh
```

Then verify Docker:

```bash
docker --version
```

### Linux

Install Docker using your distro package manager and verify:

```bash
docker --version
```

## Build

The build wrappers automatically ensure the pinned toolchain image is available.

### Build everything for testing

Windows PowerShell:

```powershell
.\build\scripts\build.ps1 build-all
```

macOS/Linux:

```bash
./build/scripts/build.sh build-all
```

### Agent tool wrappers (`os-*`)

Automated agents (and CI scripts) should prefer the deterministic
`build/scripts/os-*` wrappers over invoking raw `*.sh` directly. Each
wrapper emits a stable JSON envelope on stdout and mirrors it into the
per-run bundle at `artifacts/runs/<run_id>/<tool>.json`:

```bash
export SECUREOS_RUN_ID="cron-$(date -u +%Y%m%dT%H%M%SZ)"
./build/scripts/os-build image
./build/scripts/os-package
./build/scripts/os-run-qemu --test kernel_console
./build/scripts/os-validate
./build/scripts/os-snapshot
```

Full contract: [`docs/test-plans/wrappers.md`](docs/test-plans/wrappers.md).
Raw scripts remain supported for humans and ad-hoc invocation.

### Common build targets

Windows PowerShell:

```powershell
.\build\scripts\build.ps1 kernel
.\build\scripts\build.ps1 image
.\build\scripts\build.ps1 disk
```

macOS/Linux:

```bash
./build/scripts/build.sh kernel
./build/scripts/build.sh image
./build/scripts/build.sh disk
```

## Run the Console

### Headless console (no graphics)

This launches an interactive SecureOS prompt using the containerized QEMU path.

Windows PowerShell:

```powershell
.\build\scripts\build.ps1 console
```

macOS/Linux:

```bash
./build/scripts/build.sh console
```

Notes:
- Type commands at the `secureos>` prompt.
- Use `exit pass` to stop QEMU cleanly.

### Graphical console

This launches QEMU with a graphical window and serial console input.

Windows PowerShell:

```powershell
.\build\scripts\build.ps1 graphics
```

macOS/Linux:

```bash
./build/scripts/build.sh graphics
```

Notes:
- Keep typing commands in the terminal serial console.
- The QEMU graphics window is for display output.
- If graphics mode fails, confirm `qemu-system-x86_64` is installed and available on PATH.

## Demo Applications

After launching the console (`secureos>` prompt), you can explore and run demo apps.

### List available apps

At the prompt:

```text
apps
```

This prints the currently discoverable packaged apps on disk.

### Run the filedemo app

At the prompt:

```text
run filedemo
cat appdemo.txt
```

What this demonstrates:
- Launching a packaged user app through the process runtime
- Writing data to the filesystem from user space
- Reading the generated file back through the shell

### Networking demos

SecureOS also includes networking-focused demos/commands (for example `ifconfig`, `ping`, and `http`).
Use `help` in the console to see available command usage and then execute them directly at `secureos>`.

### Exiting the demo session

At the prompt:

```text
exit pass
```

This exits QEMU cleanly and returns control to your host terminal.

## Build Artifacts and Tool Binaries

Host build artifacts under `tools/*/` (e.g. `tools/sof_wrap/sof_wrap`) must **not** be committed. They are rebuilt deterministically by their adjacent `Makefile` and are ignored via `.gitignore`. See issue #101 for the regression that motivated this rule.

## Validate Artifacts

After successful builds, key outputs include:
- `artifacts/kernel/secureos.iso`
- `artifacts/disk/secureos-disk.img`
- `artifacts/os/*.bin`
- `artifacts/user/**/*.bin`
- `artifacts/lib/*.lib`

## CI stages

SecureOS runs multiple CI workflows with distinct scope:

- **Lint** (`.github/workflows/lint.yml`): fast stage-1 checks (format/shell/static parity) on PRs and pushes to `main`.
- **PR Build Validation** (`.github/workflows/pr-build.yml`): full validation bundle for pull requests.
- **ISO Build And VM Smoke Test** (`.github/workflows/iso-vm-build.yml`): boot/build smoke on PRs and pushes/tags.
- **Scheduled Drift Gates** (`.github/workflows/scheduled-drift-gates.yml`): weekly Monday 12:00 UTC + manual dispatch run of lint + host drift validators against `main` during merge stalls.

The scheduled drift-gate workflow is observability-only and does **not** gate PR merges.

## Coding and Planning Expectations

Before opening a PR, review:
- `AGENTS.md`
- `docs/CODING_CONVENTIONS.md`
- `docs/architecture/decisions/` (ADR index — read the relevant ADRs before changing boot, ABI, or other locked contracts)
- `docs/abi/` — ABI reference (syscall surface, capability IDs, manifest schema, versioning policy). Update the relevant page in the same PR when you change ABI surface.
- `docs/abi/capability-deny-contract.md` (if your change touches a capability-gated service or its deny-path test)
- `docs/test-plans/` (milestone + task registry; update the `status:` and `pr:#…` tags when a CAP-\* / M-\* task lands)

Project-specific expectations include:
- Keep PowerShell and shell build scripts in sync. The `parity` test target
  (`build/scripts/test.sh parity` / `build/scripts/test.ps1 parity`) walks
  `build/scripts/` and fails on unallowlisted `.sh` ↔ `.ps1` drift; intentional
  asymmetries belong in `build/scripts/.shell_parity_allowlist` with a comment
  explaining the exemption (see issue #156 / `plans/2026-05-16-shell-script-parity.md`).
- Add plan documents under the top-level `plans/` directory (canonical, single location — not `docs/plans/`) using the naming convention `YYYY-MM-DD-<slug>.md` for major implementation work; index and grouping in `plans/README.md`
- `dev/hello.c` is SHA-pinned (`tests/host/pins/dev_hello_c.sha256`). Any intentional edit to `dev/hello.c` must update that pin and re-run `./build/scripts/test.sh dev_hello_c_pin` (plus the #619 hello-SOF golden refresh workflow when that gate is in use).
- Keep hardware access behind HAL abstractions
- Commit any new `build/scripts/*.sh` validator/build script with the executable bit set (`git update-index --chmod=+x <path>`). The CI validator bundle invokes these scripts directly; a missing exec bit silently fails the run with `Permission denied`. See issue #90.
- Run `build/scripts/lint.sh` (or `build/scripts/lint.ps1` on Windows) before pushing. This covers BUILD_ROADMAP §6.1 stage 1 (clang-format, shellcheck, `.sh` ↔ `.ps1` parity) and is enforced in the `Lint` CI workflow.

### Breaking a merge stall

When #620-style velocity reporting shows `days_since_last_merge > 14`, prefer
landing an already-ready issue before opening another backlog item.

1. Run `python3 tools/list_ready_now_issues.py`.
2. Pick from the surfaced candidates (no open dependency refs, docs/CI/stamp slices).
3. Optionally apply a triage label with
   `python3 tools/list_ready_now_issues.py --apply-label --apply-limit 5`.

Reference: issue #626 (ready-now index) and
`docs/development/list-ready-now-issues.md`.

For drift-gate backlog visibility (sibling practice to the drift-gate
authoring guide work tracked in #616), run
`python3 tools/summarize_m7_backlog.py` during the daily-review cron pass and
again before merging key M7 gating slices (especially #410). The script emits
`artifacts/m7-backlog/summary-<date>.json` and a one-screen markdown summary so
triage can see which harnesses would flip when each gate closes.

- For decisions that pin a wire format, ABI, boot/loader contract, or other
  durable invariant, add an ADR under `docs/architecture/decisions/`
  (see that directory's `README.md` for the template and cadence)

### SKIP-pinned harness cap policy (issue #641)

For M7 toolchain scaffolding entries in
`tests/m7_toolchain/markers.json`, SecureOS enforces a per-open-gating-issue
cap on SKIP-pinned harness count.

- Run `./build/scripts/test.sh skip_backlog_cap` (or
  `python3 tools/check_skip_backlog_cap.py --root .`) before opening PRs that
  add or retarget M7 markers.
- Default cap is **12** markers per **OPEN** `gatingIssue`.
- Legacy overages are grandfathered only through
  `tests/m7_toolchain/skip_backlog_cap_allowlist.json`.
- The allowlist is **remove-only**: entries should be deleted as counts fall to
  cap; do not add entries for fresh overages.
- Escape hatch: if you need to raise the cap, first comment on the referenced
  gating issue naming the concrete sub-slice that will bring the count back
  down, then update policy in the same PR.

## Daily Review Cron

If you operate or tune the `secureos-daily-roadmap-review` cron, use
[`docs/development/daily-review-cron-prompt.md`](docs/development/daily-review-cron-prompt.md)
as the canonical prompt/rules source. The cron guidance is intentionally
versioned in-repo so anti-duplication and backlog-cap controls are reviewable.

## Boundaries & conventions

SecureOS enforces strict layering between the kernel, modules, user libraries,
and apps. Before adding a new `#include`, a new syscall, or a new top-level
directory, read:

- `docs/architecture/kernel-module-boundaries.md` — allowed include direction,
  what belongs in `kernel/` vs `user/libs/` vs `user/apps/`, the
  capability-gate rule, the HAL rule, and worked do/don't examples.
- `docs/architecture/CAPABILITIES.md` — the capability ID registry.

Reviewers should reject PRs that cross a layer (e.g. an app including a
`kernel/` header, or a service skipping its capability check) and link to the
boundaries doc.

## Pull Request Checklist

- Build succeeds locally (`build-all`)
- Relevant tests pass for your change
- New commands include help resources where applicable
- Documentation is updated for behavior changes

## Stale-Issue Triage Cadence

### 1) Trigger (machine-copyable)

Apply this cadence when both conditions are true:

- `days_since_last_merge_to_main >= 14`
- `new_skip_pinned_harness_issues_since_last_merge > 5`

Where "SKIP-pinned harness issues" are issues whose acceptance criteria include
`SKIP-pinned harness` language or add markers tied to an existing `gatingIssue`.

### 2) Daily-review cron behavior when triggered

- Prefer **not** filing another SKIP-pinned harness against the same stalled
  gating issue when it adds no new immediate executable value.
- Prefer filing a fully unblocked slice (docs, drift-gate, portability,
  tooling) that can merge during the stall.
- Prefer updating a rolling backlog/velocity issue instead of creating a new
  near-duplicate issue.

### 3) Human triager behavior when triggered

- Inspect the gating issue first (for example #408/#409/#410 in M7).
- If the gate is concretely blocked, prioritize the blocker.
- If the gate is not blocked, split the gate into smaller executable slices and
  prioritize those over additional deferred harness prework.

### 4) Explicitly acceptable during a stall

- Zero-gate drift catchers (hash pins, symbol drift gates).
- Docs pins with `Last verified against commit: <sha>` freshness stamps.
- Orientation docs that reduce contributor spin-up time.

### 5) Non-goal

This cadence is **not** a merge-policy change, branch-protection change, or
issue-label taxonomy change. It is a shared triage decision rule for
human + automation authorship cadence.
