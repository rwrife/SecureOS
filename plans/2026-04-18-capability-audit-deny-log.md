# 2026-04-18 Capability Audit + Deny Log Slice

## Goal
Add a small, zero-trust observability slice that records denied capability checks and launcher grants in a structured, testable way without changing policy semantics.

## Scope
- Keep deny-by-default behavior unchanged.
- Add a narrow audit record for capability decisions made at launch or syscall boundary.
- Preserve existing console, launcher, and filesystem behavior.
- Make the output deterministic enough for tests and CI logs.

## Plan
### Phase 1, audit record shape
- Define a minimal capability decision record with subject, capability, action, outcome, and reason.
- Keep the schema small and stable, with no ambient IDs or free-form text beyond a reason code.
- Add a single serialization format used by tests and logs.

### Phase 2, capture points
- Emit a record when the launcher grants a capability.
- Emit a record when a syscall or service boundary denies access.
- Avoid recording successful hot-path calls unless they change privilege state.

### Phase 3, storage and visibility
- Route audit output to the serial log first.
- Optionally keep a bounded in-memory ring for later inspection by tests.
- Ensure audit data does not grant access or alter control flow.

### Phase 4, validation
- Add one allow-path test showing a launcher grant is recorded.
- Add one deny-path test showing an unauthorized request is recorded as denied.
- Add one regression test proving audit logging does not change the deny result.

## Exit Criteria
- Capability decisions are visible in a structured form.
- Denials remain denials.
- Tests prove grant, deny, and non-interference behavior.
