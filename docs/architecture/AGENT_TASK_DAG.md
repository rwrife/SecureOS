# Agent Task DAG Schema

This document defines the machine-readable task DAG used to coordinate planning, implementation, and validation work for SecureOS.

## Goals

- deterministic execution contracts
- explicit dependency edges
- machine-verifiable completion criteria
- compatibility with zero-trust review/audit workflows

## Files

- Schema: `manifests/task-dag.schema.json`
- Example: `manifests/task-dag.example.json`

## Rules

1. Every task must have a unique `taskId`.
2. `dependsOn` must reference existing `taskId` values.
3. `ownerRole` is constrained to planner/implementer/test-engineer/validator.
4. `passCondition` must be structured and evaluable (no free-form "looks good").
5. Artifacts must be explicit when a task produces verification evidence.

## Validation (recommended)

Use any Draft 2020-12 JSON schema validator:

```bash
# Example with ajv-cli if installed
ajv validate -s manifests/task-dag.schema.json -d manifests/task-dag.example.json
```

## Zero-trust alignment

The DAG model ensures privileged transitions (build→run→validate→merge) are explicit and auditable. It supports deny-by-default process controls by making required checks and pass conditions first-class data.
