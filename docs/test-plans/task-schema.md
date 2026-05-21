# Task DAG schema (walk-through)

The normative schema for SecureOS agent task DAGs lives at
[`manifests/task-dag.schema.json`](../../manifests/task-dag.schema.json).
This document is a short, human-readable companion — one page, no
re-statement of the schema fields, just the shape and the conventions that
the milestone registry layers on top.

## Shape at a glance

A task-DAG document has three top-level fields:

```yaml
version: "1.0"
pipeline:
  name: <pipeline-id>
  entryTasks: [<TASK_ID>, ...]
tasks:
  - taskId: <TASK_ID>            # ^[A-Z0-9_-]+$
    ownerRole: planner | implementer | test-engineer | validator
    dependsOn: [<TASK_ID>, ...]  # optional
    run: "<command>" | ["<cmd1>", "<cmd2>"]
    artifacts: ["<path-or-tag>", ...]   # optional
    passCondition:
      type: exit_code | log_marker | expression
      # plus one of: expected | marker | expression
```

`additionalProperties: false` is enforced on every object level, so the
registry cannot introduce new top-level keys without a schema bump. The
registry encodes documentation-only metadata (status, PR/issue refs) inside
the free-form `artifacts[]` array — see "Conventions" below.

## Pass condition shapes

| `type`       | Required sibling | Example                                              |
| ------------ | ---------------- | ---------------------------------------------------- |
| `exit_code`  | `expected`       | `expected: 0`                                        |
| `log_marker` | `marker`         | `marker: "QEMU_PASS:hello_boot"`                     |
| `expression` | `expression`     | `expression: "exit_code == 0 && log_has('TEST:PASS:cap_model_skeleton')"` |

Validators consume `passCondition` to decide whether a task is green; agents
consume `run` to know what to execute and `dependsOn` to topologically order
work.

## Conventions used by `m0-m1-plan.yaml`

The registry is execution-shaped (every entry has a `run` and a
`passCondition`) so it validates against the schema as-is. Documentation
metadata is encoded as string tags in `artifacts[]`:

- `status:done` / `status:in_progress` / `status:pending` — current state.
- `pr:#<num>` — merged or open PR that lands the task.
- `issue:#<num>` — originating issue.
- Real artifact paths (e.g. `artifacts/qemu/hello_boot.log`) can co-exist
  with the tags above.

Example task entry (CAP-007, capability core, done):

```yaml
- taskId: M1_CAP_007
  ownerRole: implementer
  dependsOn: [M1_CAP_006]
  run: "./build/scripts/test.sh capability_serial_gate"
  artifacts:
    - "status:done"
    - "pr:#46"
    - "issue:#45"
  passCondition:
    type: log_marker
    marker: "TEST:PASS:cap_serial_write_gate"
```

## Validating a registry

The schema is plain JSON Schema (draft 2020-12). A registry can be checked
locally with `jsonschema`:

```bash
python3 - <<'PY'
import json, yaml, jsonschema
schema = json.load(open("manifests/task-dag.schema.json"))
plan   = yaml.safe_load(open("docs/test-plans/m0-m1-plan.yaml"))
jsonschema.validate(plan, schema)
print("OK")
PY
```

See [`manifests/task-dag.example.json`](../../manifests/task-dag.example.json)
for a minimal two-task example.
