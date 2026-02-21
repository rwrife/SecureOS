# 2026-02-21 — M1-CAP-011 audit ring overflow contract + CI assertions

## Goal
Harden capability-audit observability by making ring-buffer overflow behavior explicit, testable, and surfaced in CI artifacts.

CAP-009/010 added deterministic audit events for checks and mutations. CAP-011 locks down what happens when event volume exceeds ring capacity so downstream validation can trust audit summaries under pressure.

## Zero-trust alignment
- No new implicit grants or policy bypass paths.
- Enforcement behavior remains deny-by-default and unchanged.
- Work is observability + validation hardening only.

## Scope (incremental)
1. Define explicit overflow contract in code/docs:
   - bounded FIFO semantics (latest events retained)
   - monotonic dropped/overwritten counter semantics
   - deterministic marker fields for test assertions
2. Extend capability audit state API for test/CI visibility:
   - expose overflow/drop count readout
   - keep API bounded and side-effect free
3. Add deterministic tests for overflow scenarios:
   - exact-capacity fill
   - over-capacity wrap and ordering guarantees
   - mixed op stream (`CHECK/GRANT/REVOKE`) with overflow
4. Emit machine-readable overflow summary in validation artifacts consumed by CI.
5. Update docs + registry status for `M1-CAP-011` lifecycle (`todo`→`in-progress`→`done` by merge state).

## Files expected
- `kernel/cap/capability.h`
- `kernel/cap/capability.c`
- `tests/capability_audit_test.c`
- `build/scripts/test.sh`
- `artifacts/` validation summary producer/fixtures
- `docs/architecture/CAPABILITIES.md`
- `docs/planning/M0-M1-task-registry.md`

## Validation
- `./build/scripts/test.sh capability_audit`

## Definition of done
- Overflow behavior is specified and verified with deterministic assertions.
- CI-visible artifact includes overflow/drop summary suitable for machine checks.
- Existing CAP-009/010 behavior remains stable.
- One small PR with green validation and docs/registry closeout included.

## Out of scope
- Runtime/userland live audit streaming.
- Persistent audit log storage.
- New capability IDs beyond existing M1 set.

## Risks + mitigations
- **Risk:** Non-deterministic overflow assertions in CI.
  - **Mitigation:** fixed event sequences + explicit expected ordering fixtures.
- **Risk:** API expansion leaks mutable internals.
  - **Mitigation:** read-only summary accessors and bounded struct contract.
