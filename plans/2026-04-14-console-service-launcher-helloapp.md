# 2026-04-14 Console Service + Launcher + HelloApp Slice

## Goal
Deliver the next zero-trust vertical slice for SecureOS: a minimal console service path, launcher-mediated capability grants, and a HelloApp that can print only when explicitly authorized.

## Scope
- Keep read-only console/status flows available.
- Make app output go through the console service boundary, not direct ambient access.
- Route console-write grants through the launcher, with deny-by-default behavior.
- Keep the slice small, deterministic, and easy to validate in CI.

## Plan
### Phase 1, service boundary
- Define the minimal console service interface used by apps and the launcher.
- Ensure direct write attempts fail closed when the console-write capability is absent.
- Preserve read-only inspection commands without widening write access.

### Phase 2, launcher mediation
- Add or tighten launcher-side grant wiring for console-write.
- Make the grant flow explicit and auditable.
- Keep HelloApp unaware of any implicit write permission.

### Phase 3, validation
- Add one allow-path test for granted HelloApp console output.
- Add one deny-path test for missing console-write access.
- Add one regression test proving launcher mediation is required.

## Exit Criteria
- Console output is denied unless explicitly granted.
- Launcher-mediated access works end to end.
- Tests prove both allow and deny behavior.
- The slice remains small enough to merge cleanly.
