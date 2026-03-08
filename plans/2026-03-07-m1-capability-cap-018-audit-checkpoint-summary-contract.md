# SecureOS Plan — M1-CAP-018 Audit checkpoint summary contract + validator enforcement

## Why this matters
M1-CAP-017 added tamper-evident checkpoint seals, but CI-level artifact validation still only enforces retained/dropped event counts. Zero-trust-by-default requires machine-verifiable evidence that checkpoint integrity signals are present and internally consistent in produced artifacts.

This slice extends the capability-audit summary contract so checkpoints are always surfaced and validated, making silent regressions in checkpoint emission detectable in CI.

## Scope (incremental)
1. Extend `capability_audit` test summary artifact with checkpoint metadata:
   - `checkpointCount`
   - `latestCheckpointId`
   - `latestCheckpointSeal`
   - `latestCheckpointDroppedCount`
2. Add deterministic audit test marker output for checkpoint summary extraction.
3. Update `validate_bundle.sh` summary contract checks to require and validate these fields.
4. Update capability architecture docs and task registry status progression.
5. Keep capability authorization decisions unchanged (audit artifact + validator only).

## Non-goals
- Changing capability grant/revoke/check authorization semantics.
- Introducing cryptographic signatures or external key management.
- Modifying QEMU boot path behavior.

## Files expected to change
- `tests/capability_audit_test.c`
- `build/scripts/test_capability_audit.sh`
- `build/scripts/validate_bundle.sh`
- `docs/architecture/CAPABILITIES.md`
- `docs/planning/M0-M1-task-registry.md`

## Acceptance criteria
- `artifacts/tests/capability_audit_summary.json` includes checkpoint fields.
- Validator fails when checkpoint fields are missing/invalid.
- Validator passes when summary fields are present and consistent.
- Validation commands succeed:
  - `./build/scripts/test.sh capability_audit`
  - `./build/scripts/validate_bundle.sh`

## Execution plan
1. Emit a `TEST:AUDIT_CHECKPOINT_SUMMARY:` marker from `capability_audit_test`.
2. Parse checkpoint marker in `test_capability_audit.sh` and populate JSON summary fields.
3. Enforce field presence/type/range invariants in `validate_bundle.sh`.
4. Update docs and set task lifecycle in registry (`todo` → `in-progress` → `done` via issue/PR merge).

## Risks and mitigations
- **Risk:** Shell parsing drift from test marker format.
  - **Mitigation:** Keep marker format fixed and grep/sed extraction minimal and explicit.
- **Risk:** False-positive validator failures on zero-checkpoint runs.
  - **Mitigation:** Encode explicit invariant: if `checkpointCount == 0`, latest fields must be zero.
- **Risk:** Hidden test fragility from ring/checkpoint constant changes.
  - **Mitigation:** Assert relational invariants (non-negative/monotonic expectations), not hard-coded counts.

## Ready-to-implement issue seed
Title: `M1-CAP-018: enforce checkpoint metadata in capability audit summary contract`

Body checklist:
- [ ] Emit checkpoint summary marker from `capability_audit_test`
- [ ] Include checkpoint fields in `capability_audit_summary.json`
- [ ] Enforce checkpoint field contract in `validate_bundle.sh`
- [ ] Update capability docs + task registry status
- [ ] Validate with `./build/scripts/test.sh capability_audit` and `./build/scripts/validate_bundle.sh`
