# 2026-04-11 Console Service + Launcher Capability Slice

## Goal
Deliver the next zero-trust vertical slice: a console service and launcher path where app output is capability-gated, deny-by-default, and validated with both allow and deny tests.

## Scope
- Add a small console service boundary for text output.
- Add launcher mediation so apps only receive console-write capability when explicitly granted.
- Keep status/read-only commands available without widening write access.
- Preserve the existing network hardening direction by not coupling this slice to network work.

## Plan
### Phase 1, explicit capability boundary
- Define a minimal console-write capability in the app/module manifest path.
- Route app output through one launcher-mediated entrypoint.
- Ensure unauthorized writes fail closed with a stable error.

### Phase 2, launcher and service wiring
- Add a console service shim used by the launcher and app runtime.
- Make grant flow explicit in the launcher, not implicit in the app.
- Keep read-only inspection commands separate from output permissions.

### Phase 3, tests
- Add one allow-path test for granted console output.
- Add one deny-path test for missing console-write capability.
- Add one regression test that launcher mediation is required for app output.

## Exit criteria
- App output is denied unless console-write is explicitly granted.
- Launcher-mediated grant flow works end to end.
- Tests prove both allow and deny paths.
- Changes stay small enough to review and merge independently.
