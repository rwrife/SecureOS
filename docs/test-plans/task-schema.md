# Task DAG Schema

> Canonical record shape for planner / implementer / test-engineer / validator
> agents. Anchors `BUILD_ROADMAP.md` §4.1 ("Task model") and §8 item 11.
> Referenced from `AGENTS.md` and from the milestone task registry in
> `docs/test-plans/` (see #109).

This file defines the **fields**, **types**, and **invariants** of one task
record. It does **not** define a validator implementation — `os-validate`
(see `BUILD_ROADMAP.md` §4.3 and issue #162) is the consumer. Reports emitted
by validators (see `validator_report.json`, issue #110) reuse the same field
vocabulary so a task and its result can be joined by `task_id` alone.

## 1. File format

- Registries live under `docs/test-plans/` (e.g.
  `docs/test-plans/m0-m1-plan.yaml`, see §8 item 12).
- One file per milestone; each file is a YAML mapping with a single top-level
  key `tasks:` whose value is a list of task records described below.
- Comments (`#`) are allowed. No anchors / merge keys — keep registries
  trivially parseable.
- Field order in a record is not significant, but the canonical order in
  examples below should be preferred for readability.

## 2. Required fields

| Field              | Type            | Notes                                                                                              |
| ------------------ | --------------- | -------------------------------------------------------------------------------------------------- |
| `task_id`          | string          | Stable identifier. Format: `<MILESTONE>-<AREA>-<NNN>`, e.g. `M1-CAP-009`. ASCII, uppercase, no spaces. |
| `milestone`        | string          | One of `M0`, `M1`, `M2`, `M3`, `M4`, `M5`, `M6` (matches `BUILD_ROADMAP.md` §5).                   |
| `owner_role`       | enum            | One of `planner`, `implementer`, `test_engineer`, `validator`. Matches §4.2 role contracts.        |
| `depends_on`       | list&lt;string&gt;    | Zero or more `task_id` values that must reach `status: done` first. Cycles are an error.           |
| `run`              | string          | Single shell command. Must be invokable from repo root inside the pinned toolchain container.      |
| `expected_outputs` | list&lt;string&gt;    | Repo-relative artifact paths the task is expected to produce or update (`artifacts/...`, etc.).     |
| `pass_condition`   | string          | Boolean expression over `exit_code` and `log_has('<marker>')` (see §3).                           |
| `status`           | enum            | One of `pending`, `in_progress`, `done`, `blocked`. Validator-writable only.                       |

## 3. `pass_condition` expression DSL

Minimal, total, side-effect-free. A `pass_condition` is a boolean expression
built from:

- Literals: `true`, `false`, integer literals.
- Variables:
  - `exit_code` — the process exit code of `run`.
- Functions:
  - `log_has('<marker>')` — true iff the marker substring appears literally in
    the captured serial log for this task's run.
- Operators: `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!`, parentheses.

The harness already emits the structured markers defined in
`BUILD_ROADMAP.md` §2.5:

- `TEST:START:<name>`
- `TEST:PASS:<name>`
- `TEST:FAIL:<name>:<reason>`

So the canonical positive-path condition is:

```
exit_code == 0 && log_has('TEST:PASS:<name>')
```

and the canonical negative-path condition is:

```
exit_code != 0 && log_has('TEST:FAIL:<name>')
```

Out of scope for this version of the DSL: regex, arithmetic on `exit_code`,
multi-line log assertions, JSON path queries. Add them in a follow-up only
when a concrete task requires them.

## 4. Optional fields

| Field         | Type         | Notes                                                                                  |
| ------------- | ------------ | -------------------------------------------------------------------------------------- |
| `description` | string       | One-line human summary. Encouraged on every task.                                      |
| `issue`       | integer      | Linked GitHub issue number (no `#`).                                                   |
| `pr`          | integer      | Linked PR number once filed.                                                           |
| `timeout_s`   | integer      | Wall-clock timeout for `run`. Defaults to harness default if omitted.                  |
| `notes`       | string       | Free text. Validators must not parse this.                                             |

Unknown fields are an error — registries must round-trip without loss.

## 5. Status transitions

```
pending ──► in_progress ──► done
   │             │
   └────────► blocked ◄──────┘
```

- Only the **validator** role mutates `status`.
- `in_progress → done` requires the most recent run to satisfy
  `pass_condition` and produce every entry in `expected_outputs`.
- `blocked` is allowed from any state; transition out of `blocked` requires
  re-running the task.

## 6. Worked example

The example below is **M1-CAP-009 audit ring**, modeled after the existing
capability-audit slice (PR #98, issue #84). It exercises every required
field, both kinds of `pass_condition`, and a `depends_on` edge to the
preceding console-write slice.

```yaml
tasks:
  - task_id: M1-CAP-009
    milestone: M1
    owner_role: test_engineer
    description: >
      Structured capability audit log line + non-interference test for the
      console-write capability slice.
    depends_on:
      - M1-CAP-008          # launcher-mediated console-write slice (#81)
    run: build/scripts/test.sh capability_audit
    expected_outputs:
      - artifacts/qemu/capability_audit.log
      - artifacts/runs/latest/validator_report.json
    pass_condition: exit_code == 0 && log_has('TEST:PASS:capability_audit')
    status: pending
    issue: 84
    pr: 98
    timeout_s: 120

  - task_id: M1-CAP-009-NEG
    milestone: M1
    owner_role: test_engineer
    description: >
      Negative-path companion: deny console-write and assert the audit ring
      records a DENY entry with the documented marker.
    depends_on:
      - M1-CAP-009
    run: build/scripts/test.sh capability_audit_deny
    expected_outputs:
      - artifacts/qemu/capability_audit_deny.log
    pass_condition: exit_code != 0 && log_has('TEST:FAIL:capability_audit_deny')
    status: pending
    issue: 84
```

## 7. Relationship to harness markers and validator reports

- The `TEST:START` / `TEST:PASS` / `TEST:FAIL` markers from
  `BUILD_ROADMAP.md` §2.5 are the **only** log-side contract a
  `pass_condition` should rely on; tasks that need richer assertions should
  emit additional structured markers from the test program itself rather
  than parsing free-form output.
- `validator_report.json` (see #110) re-emits, per task, at minimum:
  `task_id`, `status`, `exit_code`, the resolved boolean value of
  `pass_condition`, and the absolute paths of every `expected_outputs`
  entry that was actually produced. Consumers should treat
  `validator_report.json` as the canonical post-run view of a task record.
- `os-validate` (see #162) is the deterministic wrapper that loads a
  registry, executes selected tasks in dependency order, and writes the
  report.

## 8. Out of scope

- A reference loader / validator implementation.
- Backfilling all historical tasks into a registry (that work is tracked by
  #109).
- A richer expression language for `pass_condition` (add fields only as
  concrete tasks require them).

## 9. Cross-references

- `BUILD_ROADMAP.md` §2.5 (harness markers), §4.1 (task model), §4.3
  (deterministic wrappers), §4.4 (artifact policy), §8 item 11 (this schema).
- `AGENTS.md` (agent role conventions).
- Issues: #109 (registry consumer), #110 (validator JSON report), #162
  (`os-validate` wrapper), #164 (capability-denied log marker contract).
