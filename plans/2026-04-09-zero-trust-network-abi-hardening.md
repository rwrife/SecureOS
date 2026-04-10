# 2026-04-09 Zero-Trust Network ABI Hardening

## Goal
Make the user-space network surface explicit, capability-gated, and regression-tested now that HTTPS stubs and TLS plumbing are present.

## Phase 1
- Add a small capability gate wrapper for all netlib entrypoints that touch external network I/O.
- Make `http` and `https` command paths share one URL dispatch path so `https://` never bypasses the deny-by-default checks.
- Add negative-path tests for missing `CAP_NETWORK` and invalid URL schemes.
- Keep `ifconfig` read-only status reporting available without widening write access.

## Phase 2
- Separate transport selection from request formatting in `netlib`.
- Add one validation case for HTTP, one for HTTPS, and one for DNS/TCP failure reporting.
- Document the ABI/capability boundary in `docs/architecture/CAPABILITIES.md`.

## Exit Criteria
- All network-facing commands fail closed without `CAP_NETWORK`.
- HTTPS reuses the same policy path as HTTP.
- Tests prove one deny path and one allow path for each transport.
