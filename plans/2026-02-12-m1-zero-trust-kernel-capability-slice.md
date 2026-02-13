# SecureOS Plan — M1 Zero-Trust Kernel Capability Slice

_Date: 2026-02-12_

## Why this next

Current repo status is strong on deterministic build/test harness and boot validation plumbing. The highest-leverage next feature is to land the **first enforceable kernel capability boundary** (M1 foundation), so every later service/module can default to deny and request explicit rights.

This plan is intentionally incremental and keeps each step independently verifiable.

## Scope

Implement a minimal, testable kernel capability core for the x86 boot slice with deny-by-default semantics:

- Static capability descriptors (kernel-internal for now)
- Per-process capability table (fixed-size initial implementation)
- Capability check API with explicit error reasons
- First syscall gate wired to capability enforcement
- Positive + negative tests with machine-readable markers

## Non-goals (for this slice)

- Full userspace process scheduler
- Dynamic capability delegation broker
- Manifest parser and signing
- Persistent policy store

## Architecture constraints (zero-trust-by-default)

1. No implicit grants: every privileged action must pass an explicit capability check.
2. Deny is the default path for missing/invalid capabilities.
3. Check results must be auditable in serial logs and test artifacts.
4. Capability identity must be stable and machine-readable.

## Implementation plan (incremental)

### Step 1 — Capability model skeleton

**Deliverables**
- `kernel/cap/capability.h` (or equivalent path once kernel tree exists)
- `kernel/cap/capability.c`
- Enum for capability IDs (e.g., `CAP_CONSOLE_WRITE`, `CAP_DEBUG_EXIT`)
- Error enum (`CAP_OK`, `CAP_ERR_MISSING`, `CAP_ERR_INVALID_SUBJECT`)

**Acceptance criteria**
- Unit-style kernel test for lookup/check helpers passes.
- Logs include `TEST:PASS:cap_model_skeleton`.

### Step 2 — Subject capability table (deny by default)

**Deliverables**
- `kernel/cap/table.h/.c` fixed-size capability bitmap/table
- Subject initialization with zero capabilities
- Grant helper used only by kernel bootstrap test path

**Acceptance criteria**
- Fresh subject cannot perform protected action.
- Test logs include both deny and allow markers.

### Step 3 — First gated operation

**Deliverables**
- Add one privileged kernel operation (initially serial console write syscall stub)
- Gate operation behind `CAP_CONSOLE_WRITE`
- Return explicit status code on denial

**Acceptance criteria**
- Without capability: operation denied and deterministic error returned.
- With capability: operation succeeds and emits expected serial marker.

### Step 4 — Validation harness integration

**Deliverables**
- Extend `build/scripts/test.sh` target set with `test-capability-gate`
- Add parser expectations in existing marker logic
- Persist artifacts under `artifacts/runs/<id>/` with cap-test logs

**Acceptance criteria**
- CI/local command returns non-zero when deny/allow expectations are violated.
- Machine-readable summary includes capability test result.

### Step 5 — Documentation + ADR

**Deliverables**
- `docs/architecture/capability-core.md`
- `docs/adr/0001-capability-gate-first.md`
- Define invariants, threat model assumptions, and extension path to brokered delegation.

**Acceptance criteria**
- Docs clearly state deny-by-default invariant and audit requirements.

## Proposed issue breakdown

1. **CAP-001:** capability IDs + check API skeleton
2. **CAP-002:** subject capability table + bootstrap grant path
3. **CAP-003:** console write gate + explicit denied error
4. **CAP-004:** harness/CI integration for cap tests
5. **CAP-005:** architecture docs + ADR

Each issue should be PR-sized, independently testable, and mergeable in order.

## Validation commands (target shape)

- `build/scripts/build.sh`
- `build/scripts/test.sh --test hello_boot`
- `build/scripts/test.sh --test capability_gate`

## Risks and mitigations

- **Risk:** No kernel tree scaffold yet in main branch.
  - **Mitigation:** Land directory skeleton + stubs first (CAP-001) with compile-time checks.
- **Risk:** Test markers drift across scripts.
  - **Mitigation:** Keep a single source for marker constants; fail fast in parser.
- **Risk:** Capability sprawl without naming discipline.
  - **Mitigation:** Reserve namespaced IDs and document ownership per subsystem.

## Done definition for this plan

- At least CAP-001..CAP-003 merged and runnable.
- Deny-by-default behavior proven by automated negative-path test.
- CI artifact includes capability gate verdict and logs.
