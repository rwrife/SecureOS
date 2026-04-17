# 2026-04-16 Filesystem Service + Faux FS Slice

## Goal
Deliver the next zero-trust vertical slice after console/launcher: a minimal filesystem service boundary, a real persistent namespace, and a faux ephemeral filesystem path that is explicitly granted, never ambient.

## Scope
- Keep the console/launcher capability model intact.
- Introduce a filesystem service API with deny-by-default access.
- Support two storage modes:
  - persistent ramfs-backed namespace
  - faux ephemeral filesystem for temporary app-scoped state
- Require launcher-mediated grants for any write or persistence access.
- Keep the implementation small enough to validate with deterministic allow/deny tests.

## Plan
### Phase 1, filesystem boundary
- Define the smallest filesystem service interface needed for app read/write/open operations.
- Make missing filesystem-write or filesystem-persist access fail closed.
- Preserve read-only inspection flows where possible.

### Phase 2, storage backends
- Add a ramfs-backed persistent namespace for shared data.
- Add a faux ephemeral FS provider with the same interface but isolated lifecycle.
- Ensure ephemeral data is discarded on app exit/relaunch.

### Phase 3, launcher mediation
- Route filesystem grants through the launcher manifest path.
- Keep explicit auditability for persistent-vs-ephemeral access.
- Prevent apps from assuming persistence unless granted.

### Phase 4, validation
- Add one allow-path test for granted persistent write/read.
- Add one deny-path test for missing filesystem-write access.
- Add one regression test proving ephemeral data does not survive relaunch.

## Exit Criteria
- Filesystem access is denied unless explicitly granted.
- Persistent and ephemeral storage behave differently and deterministically.
- Launcher-mediated grants are required end to end.
- Tests prove granted, denied, and lifecycle-reset behavior.
