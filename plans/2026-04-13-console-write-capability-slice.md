# 2026-04-13 Console Write Capability Slice

## Goal
Deliver the next zero-trust vertical slice for SecureOS: explicit console-write capability gating for user apps, with launcher-mediated grants and deny-by-default behavior.

## Scope
- Keep read-only console/status flows available.
- Make console output require an explicit capability grant.
- Route grants through the launcher or equivalent mediation layer, not from the app itself.
- Add small, deterministic tests for allow and deny paths.

## Plan
### Phase 1, capability boundary
- Define the minimal console-write capability in the app/module manifest path.
- Ensure app output requests fail closed when the grant is missing.
- Preserve read-only inspection commands without widening write access.

### Phase 2, launcher mediation
- Add or tighten the launcher console service shim.
- Make the grant flow explicit and auditable.
- Keep the app runtime unaware of any implicit write permission.

### Phase 3, validation
- Add one allow-path test for granted console output.
- Add one deny-path test for missing console-write access.
- Add one regression test proving launcher mediation is required.

## Exit Criteria
- Console output is denied unless explicitly granted.
- Launcher-mediated access works end to end.
- Tests prove both allow and deny behavior.
- The slice stays small enough to land cleanly.
