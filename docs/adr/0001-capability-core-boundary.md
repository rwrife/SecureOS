# ADR 0001: Capability core boundary and first privileged gate

- Status: Accepted
- Date: 2026-02-16
- Deciders: SecureOS maintainers
- Related: M1-CAP-001, M1-CAP-002, M1-CAP-003, M1-CAP-004, M1-CAP-005

## Context

SecureOS M1 establishes the first enforceable capability boundary. Prior to M1, privileged paths were implicitly allowed and lacked machine-verifiable deny-by-default behavior.

The M1 implementation introduced:

- Stable capability IDs with explicit check result codes.
- A bounded per-subject capability table with deny-by-default semantics.
- A first privileged gate for console writes (`CAP_CONSOLE_WRITE`).
- Deterministic allow/deny markers integrated into validation bundles.

Without a committed architecture decision, future capability work risks drift in error semantics, ID stability, and validation contracts.

## Decision

Adopt the capability core boundary with these explicit architectural rules:

1. **Capability IDs are stable and append-only**
   - Existing numeric IDs are never renumbered.
   - Unknown IDs return explicit invalid-capability errors.

2. **Checks are deny-by-default with explicit outcomes**
   - Success is only possible with an explicit grant.
   - Missing grants and invalid inputs are distinct error states.

3. **Capability storage is bounded by subject capacity**
   - Subject identifiers are validated at API boundaries.
   - Invalid subjects return explicit invalid-subject errors.

4. **Privileged operations must route through capability gates**
   - The first gate is console write.
   - New privileged operations must add a gate before being considered complete.

5. **Validation must remain deterministic and machine-readable**
   - Capability gate tests must emit stable pass/fail markers.
   - Validation bundles must include capability marker artifacts.

## Consequences

### Positive

- Establishes a durable security contract for capability growth.
- Keeps zero-trust default behavior testable in CI.
- Reduces ambiguity for future privileged paths and review.

### Trade-offs

- Adds up-front work for each new privileged operation (gate + tests + markers).
- Requires discipline to preserve marker compatibility over time.

## Validation

Current deterministic checks:

- `./build/scripts/test.sh capability_gate`
- `./build/scripts/test.sh hello_boot`

## Follow-up

- Extend capability IDs and table internals (e.g., packed bitsets) without breaking API semantics.
- Gate additional privileged kernel operations one-by-one under the same contract.
