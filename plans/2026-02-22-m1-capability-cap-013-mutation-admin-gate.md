# Plan — M1-CAP-013: Capability Mutation Admin Gate

## Why this next

CAP-012 locked audit-summary schema validation in CI, which hardens observability. The next zero-trust increment is to harden **who is allowed to mutate capability grants** so grant/revoke operations are not implicitly trusted side effects.

## Scope

Introduce an explicit admin capability boundary for capability mutations:

1. Add a dedicated capability ID for capability-administration operations.
2. Add mutation APIs that require an authorized actor subject.
3. Enforce deny-by-default behavior for unauthorized mutation attempts.
4. Preserve deterministic audit behavior for successful and failed mutation paths.

## Non-goals

- Introducing role/group abstractions beyond current subject/cap model.
- Persisting capability state beyond current in-memory test scope.
- Expanding privileged operation gates beyond existing console/serial/debug paths.

## Implementation slices

### Slice A — Capability contract expansion
- Add `CAP_CAPABILITY_ADMIN` as the next stable append-only capability ID.
- Update architecture docs and API contract tests to lock ID stability.

### Slice B — Admin-gated mutation API
- Add actor-aware mutation entry points (grant/revoke with actor subject parameter).
- Require actor subject to hold `CAP_CAPABILITY_ADMIN` before mutating another subject's grants.
- Keep deny-by-default semantics (`CAP_ERR_MISSING` when actor lacks admin capability).

### Slice C — Deterministic tests + audit coverage
- Add/extend tests for:
  - unauthorized actor mutation denied
  - authorized actor mutation allowed
  - invalid actor/target/cap IDs rejected deterministically
- Ensure mutation outcomes continue to appear in capability audit stream.

## Acceptance criteria

- New admin capability ID is stable and documented.
- Unauthorized mutation attempt is denied by default.
- Authorized actor can grant/revoke existing privileged capabilities deterministically.
- Existing validation suites remain green.

## Verification commands

- `./build/scripts/test.sh cap_api_contract`
- `./build/scripts/test.sh capability_table`
- `./build/scripts/test.sh capability_audit`
- `./build/scripts/validate_bundle.sh`

## Risks and mitigations

- **Risk:** Test bootstrap paths become circular if admin capability is required before first grant.
  - **Mitigation:** Keep test-only bootstrap helpers explicit and separate from actor-gated mutation entry points.
- **Risk:** Capability-ID drift as enum grows.
  - **Mitigation:** Lock ID values with explicit contract assertions in tests and docs.
