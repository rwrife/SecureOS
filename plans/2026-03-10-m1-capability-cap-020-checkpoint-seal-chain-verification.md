# M1-CAP-020 Plan — Capability Audit Checkpoint Seal-Chain Verification

## Why this next
CAP-019 enforces sequence-window consistency between retained events and checkpoint summaries, but artifact consumers still trust each checkpoint seal in isolation. For zero-trust-by-default, validators should reject histories where checkpoint lineage is spliced, reordered, or partially replayed even when window fields look plausible.

## Goal
Add deterministic checkpoint seal-chain verification so exported audit artifacts prove checkpoint-to-checkpoint continuity and fail closed on tampering.

## Scope (incremental)

### Slice A — Contract extension (schema + docs only)
1. Extend checkpoint summary artifact entries to include:
   - `prev_checkpoint_id`
   - `prev_checkpoint_seal`
   - `chain_status` (`genesis|linked`)
2. Document invariants in `docs/architecture/CAPABILITIES.md` and `docs/planning/M0-M1-task-registry.md`.
3. Add/refresh deterministic fixtures with both genesis and linked checkpoints.

Validation:
- `./build/scripts/test.sh capability_audit`
- `./build/scripts/validate_bundle.sh`

### Slice B — Validator chain rules
1. Add validator assertions:
   - First checkpoint must be `chain_status=genesis` with null/absent previous pointers.
   - Every non-genesis checkpoint must reference the immediate predecessor id and predecessor seal.
   - Checkpoint IDs must be contiguous and strictly monotonic.
   - Any mismatch returns explicit validator error codes/messages.
2. Add negative fixtures for splice/reorder/forged-prev-pointer cases.

Validation:
- `./build/scripts/validate_bundle.sh`
- focused negative fixture invocations under `tests/`

### Slice C — Runtime/fixture tamper proofs
1. Bind checkpoint construction tests to emit explicit predecessor linkage markers.
2. Add deterministic tests that mutate predecessor id/seal and prove validator rejection.
3. Keep PR narrow: no crypto primitives, no key management.

Validation:
- `./build/scripts/test.sh capability_audit`
- `./build/scripts/validate_bundle.sh`

## Non-goals
- No cryptographic signing or external trust root integration in CAP-020.
- No capability policy/runtime authorization semantic changes.
- No expansion of retention-window size constraints.

## Definition of done
- Validator fails closed on checkpoint lineage tampering (splice/reorder/forged linkage).
- Positive and negative tests pass deterministically in CI.
- Docs/task registry updated with CAP-020 contract and commands.

## Risks / blockers
- Host environments missing `nasm` can fail full bundle validation; CI should remain source-of-truth for complete pass/fail.
- Fixture drift between audit and checkpoint summaries can cause noisy failures if landed in oversized PRs.

## First implementation PR target
Start with Slice A only (schema/docs/fixtures) to keep review small, verifiable, and easy to iterate.