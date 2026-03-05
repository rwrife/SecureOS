# SecureOS Plan — M1-CAP-017 Tamper-evident capability audit checkpoints

## Why this matters
M1-CAP-016 established strict monotonic ordering via `sequence_id`, but sequence numbers alone do not make retained windows tamper-evident. If memory corruption or unintended writes alter retained events, current tests cannot prove integrity of what remains in the ring.

This slice adds deterministic checkpoint seals over bounded audit windows so validation can detect divergence while preserving deny-by-default enforcement semantics.

## Scope (incremental)
1. Add an internal rolling integrity accumulator for audit events that updates on every append.
2. Emit periodic checkpoint records (fixed cadence, e.g. every 8 events) containing:
   - checkpoint sequence range
   - dropped-count snapshot
   - seal digest value
3. Expose read-only test helpers for latest checkpoint metadata.
4. Add deterministic tests that verify:
   - seal changes with event content/order changes
   - checkpoints remain monotonic across ring wrap
   - dropped-count is incorporated into checkpoint state
5. Keep capability decision logic unchanged (audit-only enhancement).

## Non-goals
- Cryptographic signing or external persistence.
- Runtime policy decisions based on checkpoint values.
- Expanding capability ID surface.

## Files expected to change
- `kernel/cap/capability.c`
- `kernel/cap/capability.h`
- `tests/capability_audit_test.c`
- `docs/architecture/CAPABILITIES.md`
- `docs/planning/M0-M1-task-registry.md`

## Acceptance criteria
- New checkpoint API is deterministic in repeated test runs.
- Existing `sequence_id` invariants continue to pass.
- Added test section asserts seal monotonic progression and wrap-safe behavior.
- Validation command succeeds:
  - `./build/scripts/test.sh capability_audit`

## Execution plan
1. Introduce lightweight deterministic mixing function for audit fields.
2. Track rolling state and checkpoint emission cadence in audit recorder internals.
3. Add test retrieval API for latest checkpoint snapshot.
4. Extend `capability_audit_test` with dedicated checkpoint integrity cases.
5. Update architecture docs and set registry entry lifecycle (`todo` → `in_progress` → `done` through issue/PR flow).

## Risks and mitigations
- **Risk:** Overfitting to current ring size constants.
  - **Mitigation:** Derive checkpoint logic from append count, not static slot indices.
- **Risk:** Nondeterministic digest implementation.
  - **Mitigation:** Use fixed-width integer math and explicit cast boundaries only.
- **Risk:** Hidden coupling with authorization path.
  - **Mitigation:** Keep all changes inside audit recording/readout paths.

## Ready-to-implement issue seed
Title: `M1-CAP-017: add tamper-evident audit checkpoints`

Body checklist:
- [ ] Add rolling audit integrity accumulator and checkpoint state
- [ ] Expose deterministic checkpoint read API for tests
- [ ] Add wrap/overflow checkpoint integrity tests
- [ ] Update capability docs + task registry status
- [ ] Validate with `./build/scripts/test.sh capability_audit`
