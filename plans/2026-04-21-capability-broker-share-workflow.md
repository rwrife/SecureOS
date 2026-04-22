# 2026-04-21 Capability Broker + Share Workflow Slice

## Goal
Deliver the next zero-trust vertical slice after filesystem and audit work: a minimal capability broker that can issue, scope, and revoke a shared resource capability through an explicit workflow, with deny-by-default behavior and deterministic validation.

## Scope
- Keep existing console, launcher, filesystem, and audit semantics unchanged.
- Introduce a broker boundary for sharing a resource between principals.
- Support one narrow share path, with explicit consent or policy approval.
- Keep capability scope bounded to one named resource and one recipient.
- Record grant and deny decisions in the audit trail without changing control flow.

## Plan
### Phase 1, broker contract
- Define the smallest broker API needed to request, issue, and reject a share.
- Use stable capability identifiers and narrow metadata, no ambient read/write expansion.
- Make missing broker authority fail closed.

### Phase 2, share lifecycle
- Add a single share issuance path for one resource type.
- Add explicit revocation or expiration for issued shares.
- Ensure shares do not silently escalate into broader access.

### Phase 3, launcher and app mediation
- Route share requests through launcher-owned manifests or prompts.
- Require the recipient to present the issued capability before access is granted.
- Preserve existing deny-by-default behavior for non-participants.

### Phase 4, validation
- Add one allow-path test for an approved share.
- Add one deny-path test for an unapproved or malformed share request.
- Add one regression test proving revocation removes access.

## Exit Criteria
- Capability sharing is explicit, narrow, and revocable.
- Denials remain denials.
- Tests prove grant, deny, and revoke behavior.
