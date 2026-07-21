# M7 Toolchain Pick-Up Guide

## 1) You are here

The M7 in-OS toolchain chain is currently blocked at **#408** (TinyCC freestanding port). Earlier prerequisites are merged: **#404** (heap), **#405** (filesystem writes), and **#407** (freestanding libc baseline). Until #408 lands, later phases **#409** (sofpack + `cc` driver) and **#410** (unsigned-run wiring + acceptance suite) stay queued. Practical takeaway: if you want today’s highest-leverage work, either land a small #408 sub-slice, or land docs/drift slices that keep `main` moving while #408 is in progress.

## 2) Dependency chain (today)

```text
#404 (heap, DONE) ─┐
#405 (fs,   DONE) ─┼─▶ #407 (libc, DONE) ─▶ #408 (TinyCC, IN PROGRESS)
                                               │
                                               ▼
                                     #409 (sofpack + cc, QUEUED)
                                               │
                                               ▼
                                     #410 (unsigned-run + acceptance, QUEUED)

Downstream acceptance harness backlog: SKIP-pinned on #408/#409/#410.
```

## 3) Currently executable slices (smallest first)

1. **#538** — clib POSIX-fd nucleus (`open/close/read/lseek/unlink`).
   Why next: TinyCC libc expectations without requiring the whole compiler lift.
2. **#563** — `exit()` shim to `os_process_exit`.
   Why next: removes a concrete libc-deps blocker for freestanding userland.
3. **#564** — provide `sprintf()`.
   Why next: directly unblocks TinyCC source paths (`tccasm.c`, `tccgen.c`).
4. **#539** — close remaining libc-deps gaps.
   Why next: consolidates the remaining symbol/stub surface #408 needs.

## 4) Stall-friendly slices (fully unblocked)

If #408 implementation throughput dips, prefer slices that can merge immediately:

- **#603** — M7 exit-criteria checklist (definition-of-done clarity).
- **#616** — drift-gate authoring pattern in CONTRIBUTING.
- **#618** — `dev/building.txt` drift pin + validator.
- **#625** — stale-issue triage cadence for daily review.
- **#608** — SKIP-pinned harness authoring guide.

These are independent of code-path readiness and still reduce future delivery risk.

## 5) Don’t touch these yet (unless schedule says so)

Avoid grabbing random SKIP-pinned harnesses just because they are small. Their value is deferred unless tied to a currently-open gating issue and a concrete near-term plan. For M7, the relevant gating issues are:

- **#408** (TinyCC freestanding port)
- **#409** (sofpack + `cc` driver)
- **#410** (unsigned-run + acceptance)

Author new SKIP-pinned harnesses only when they are explicitly scoped as prework against one of those gates and not duplicating existing marker coverage.

## 6) How I know when I’m done

Use three sources:

1. `tests/m7_toolchain/markers.json` — source of truth for marker↔gatingIssue mapping.
2. `artifacts/runs/<run_id>/validator_report.json` — bundle evidence emitted by `build/scripts/validate_bundle.sh`.
3. **#603** — canonical M7 exit checklist (once merged) for umbrella-level closure.

For a sub-slice PR, prove your changed marker/contract moved from intended state to observed state, and that relevant validators pass.

## 7) Where to write things down

- Planning docs: `plans/YYYY-MM-DD-<slug>.md` (see `plans/README.md`).
- Workflow rules: `AGENTS.md` (pull `main`, create worktree, branch-per-slice).
- Drift-gate authoring convention: **#616** guidance (once merged).
- SKIP-pinned harness authoring convention: **#608** guide (once merged).

If your slice changes acceptance semantics, update both the human-facing doc and the machine-checked validator target in the same PR.

Last verified against commit: 45316d51a4dfc47e6ffa528d6cafc8b7b5195d38
