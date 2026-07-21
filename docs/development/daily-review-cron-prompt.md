# Daily Review Cron Prompt (Canonical)

Use this document as the source-of-truth prompt for the `secureos-daily-roadmap-review` cron.

## Purpose

Reduce planning/process drift by preventing repeated observations and duplicate scaffold issue churn while the merge queue is stalled.

## Required surfaces to consult every run

Before opening any new issue, the cron MUST consult these current surfaces:

- [#620](https://github.com/rwrife/SecureOS/issues/620) — merge-stall / velocity signal
- [#626](https://github.com/rwrife/SecureOS/issues/626) — ready-now index
- [#640](https://github.com/rwrife/SecureOS/issues/640) — decomposition rollup
- [#641](https://github.com/rwrife/SecureOS/issues/641) — SKIP-pinned harness cap policy

If any of these issue numbers are superseded, update this doc first so cron behavior updates with it.

## Run rules (execute in order)

1. **Read historical context first**
   - Find the latest 3 issues titled `Daily review: YYYY-MM-DD` (if present).
   - Do not restate the same observations from those issues.

2. **Prefer existing work over new scaffolds**
   - Check #626 and #640.
   - If an existing sub-slice can absorb the work, comment there instead of opening a new issue.

3. **Respect harness-cap backpressure**
   - Run `python3 tools/check_skip_backlog_cap.py --root .` (or consult its
     latest CI result) to determine current cap state.
   - Cap policy: default **12** SKIP-pinned markers per **OPEN** `gatingIssue`
     in `tests/m7_toolchain/markers.json`, with remove-only grandfathered
     exceptions in `tests/m7_toolchain/skip_backlog_cap_allowlist.json`.
   - Do not open a new pre-gating harness against a gating issue that is already at cap.

4. **Throttle issue creation while stall is unchanged**
   - Compare merge-stall metric/state in #620 versus the previous run.
   - If stall has not improved, open **at most one** new issue this run, and only if it is:
     - (a) a decomposition of an existing gating issue, or
     - (b) a direct comment/update on the ready-now index context.
   - Do **not** open new drift-gate scaffold issues in this condition.

5. **Only publish a new daily-review issue for novel signal**
   - Open `Daily review: <date>` only when there is at least one new noteworthy observation versus the last 3 daily-review issues.
   - If there is no novel signal, post a brief comment on the current coordination issue instead of opening a new daily-review issue.

## Output checklist for each run

- Cite which of #620 / #626 / #640 / #641 were consulted.
- State whether the run produced:
  - no new issue,
  - one decomposition issue, or
  - one index/comment update.
- If no issue was opened, record why (for traceability).

Last verified against commit: 257b364f95185916ac1a58987c147dc1dc8517d6
