# M1-CAP-019 Plan — Capability Audit Sequence-Window Attestation

## Why this next
M1-CAP-018 enforces checkpoint metadata presence in validation artifacts, but it does not yet assert that the exported retained event window and checkpoint stream are mutually consistent over sequence boundaries. For zero-trust-by-default, artifact consumers must be able to detect truncation/splice drift with machine-verifiable bounds.

## Goal
Add deterministic, machine-verifiable sequence-window attestation to capability audit artifacts so CI can reject bundles where retained events/checkpoints disagree about history continuity.

## Scope (incremental)

### Slice A — Artifact contract extension (small, schema-first)
1. Extend capability-audit summary JSON with:
   - `sequence_window.first_sequence_id`
   - `sequence_window.last_sequence_id`
   - `sequence_window.event_count`
   - `sequence_window.coverage` (`full|truncated`)
2. Extend checkpoint summary JSON with:
   - `checkpoint_window.first_checkpoint_id`
   - `checkpoint_window.last_checkpoint_id`
   - `checkpoint_window.count`
3. Document invariants in `docs/architecture/CAPABILITIES.md` and task registry.

Validation:
- `./build/scripts/test.sh capability_audit`
- `./build/scripts/validate_bundle.sh`

### Slice B — Validator continuity rules
1. Add validator assertions:
   - `first_sequence_id <= last_sequence_id` when `event_count > 0`
   - `event_count == 0` implies both sequence ids are null/absent
   - `coverage=truncated` requires dropped-event counter > 0
   - checkpoint window IDs monotonic and count-consistent
2. Fail fast with explicit error codes/messages for each invariant.

Validation:
- `./build/scripts/validate_bundle.sh`
- focused negative fixtures under `tests/` for each contract violation

### Slice C — Tamper-evidence cross-binding
1. Bind latest checkpoint metadata to retained sequence window in exported summary (deterministic cross-reference).
2. Add a deterministic test that mutates one side of the pair and proves validator rejection.

Validation:
- `./build/scripts/test.sh capability_audit`
- `./build/scripts/validate_bundle.sh`

## Non-goals
- No cryptographic signing/KMS integration in CAP-019.
- No runtime authorization policy changes.
- No widening of capability IDs or subject limits.

## Definition of done
- CI rejects malformed/contradictory sequence+checkpoint windows.
- Positive and negative tests are deterministic locally and in PR CI.
- Docs and task registry reflect CAP-019 status and validation commands.

## Risks / blockers
- Local environment currently reports `nasm is required`, which can block full bundle validation on this host.
- Keep PRs narrow to avoid coupling schema and heavy runtime refactors.

## First implementation PR target
Start with Slice A only (contract + docs + deterministic fixture updates) to keep review surface small and verifiable.