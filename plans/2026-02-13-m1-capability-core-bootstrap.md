# SecureOS Plan — M1 Capability Core Bootstrap (Zero-Trust by Default)

_Date: 2026-02-13_

## Why this is next

SecureOS already has deterministic boot validation and serial observability. The next highest-leverage feature is a kernel capability core that **defaults to deny** and forces explicit permission checks before privileged behavior. This creates the minimum enforceable boundary required for later launcher/service work.

## Scope (this plan)

Build the first production-usable capability slice in the kernel and validate it with deterministic allow/deny tests.

### In scope

1. Stable capability IDs + explicit check result enum.
2. Per-subject capability table initialized to zero permissions.
3. First privileged gate (`console_write`) enforced by capability checks.
4. Harness-visible test markers for allow and deny outcomes.
5. Minimal architecture documentation + ADR for invariants.

### Out of scope

- Dynamic capability delegation broker.
- Signed manifest loading.
- Full userspace process model.
- Persistent policy storage.

## Zero-trust invariants

- **No implicit privileges**: all privileged operations require explicit capability checks.
- **Deny by default**: newly created subject contexts have zero rights.
- **Auditable outcomes**: check decisions emit deterministic markers/logs.
- **Stable identities**: capability identifiers remain machine-readable and versioned.

## Implementation sequence (small, verifiable PRs)

### CAP-001 — Capability model + API contract

**Changes**
- Add `kernel/cap/capability.h` and `kernel/cap/capability.c`.
- Define stable IDs (starting with `CAP_CONSOLE_WRITE`, reserved slots for growth).
- Define result enum (`CAP_OK`, `CAP_ERR_MISSING`, `CAP_ERR_SUBJECT_INVALID`, `CAP_ERR_CAP_INVALID`).
- Add `cap_check(subject, capability)` API.

**Acceptance**
- Build passes.
- Unit-style kernel test emits `TEST:PASS:cap_api_contract`.
- Invalid subject/capability paths return explicit non-generic errors.

### CAP-002 — Subject capability table (deny-by-default)

**Changes**
- Add fixed-size per-subject capability bitmap/table (`kernel/cap/table.h/.c`).
- Add subject init helper that always zeroes grants.
- Add kernel-only grant helper for test/bootstrap path.

**Acceptance**
- Fresh subject denied for protected checks (`TEST:PASS:cap_default_deny`).
- Granted subject allowed for targeted capability only (`TEST:PASS:cap_selective_allow`).
- Regression test proves no wildcard grant behavior.

### CAP-003 — Gate first privileged operation

**Changes**
- Gate console write path behind `CAP_CONSOLE_WRITE`.
- Return deterministic error status on denial.
- Emit markers for both paths:
  - `TEST:START:cap_console_gate`
  - `TEST:PASS:cap_console_gate_deny`
  - `TEST:PASS:cap_console_gate_allow`

**Acceptance**
- Denied path returns explicit code and does not write payload.
- Allowed path writes payload and emits pass marker.
- Harness exits non-zero if either expected marker missing.

### CAP-004 — Validation + docs

**Changes**
- Extend `build/scripts/test.sh` with `capability_gate` target.
- Persist run outputs under `artifacts/runs/<id>/` including marker summary.
- Add `docs/architecture/CAPABILITIES.md`.
- Add ADR documenting deny-by-default + stable-ID policy.

**Acceptance**
- `./build/scripts/test.sh capability_gate` is deterministic locally and in CI.
- Machine-readable summary includes pass/fail for each capability test.
- Docs explain extension rules and backward-compat constraints.

## Risks and mitigations

- **Risk:** Capability checks added but not universally enforced.
  - **Mitigation:** Require marker-backed tests per gated operation.
- **Risk:** IDs churn and break downstream manifests.
  - **Mitigation:** Reserve/track IDs and ban renumbering in ADR.
- **Risk:** Hidden global state leaks privileges.
  - **Mitigation:** Subject init zeroing test + no default grants in non-test path.

## Definition of done

This plan is complete when CAP-001 through CAP-004 are merged, and CI validates deterministic allow/deny behavior for the first gated operation with deny-by-default semantics preserved.
