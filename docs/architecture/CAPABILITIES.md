# SecureOS Capabilities (M1 bootstrap)

## CAP-001 contract

SecureOS capability checks are deny-by-default and produce explicit result codes.

### Capability IDs

Current stable IDs:

- `CAP_CONSOLE_WRITE = 1`
- `CAP_SERIAL_WRITE = 2`
- `CAP_DEBUG_EXIT = 3`

Rules:

- IDs are append-only once merged.
- Existing numeric IDs are never renumbered.
- Unknown IDs must return `CAP_ERR_CAP_INVALID`.

### Check result semantics

- `CAP_OK` — subject has explicit grant.
- `CAP_ERR_MISSING` — subject is valid but capability is not granted.
- `CAP_ERR_SUBJECT_INVALID` — subject identifier is out of bounds/unknown.
- `CAP_ERR_CAP_INVALID` — capability identifier is out of bounds/unknown.

### Zero-trust invariants

- Default state is deny for all subject/capability pairs.
- No implicit grants are allowed.
- Invalid inputs are explicit errors, never treated as success.

## CAP-002 per-subject table

A fixed-capacity table now owns subject capability grants.

- `CAP_TABLE_MAX_SUBJECTS = 8`
- all subjects default to deny after `cap_table_init`/`cap_table_reset`
- grant/revoke/check are explicit API calls with bounded input validation
- invalid subject or capability IDs return explicit error codes

Current implementation stores per-subject capability grants in packed bitsets, with explicit ID validation preserving append-only capability growth.

## CAP-003 privileged console gate

The first privileged path now enforces capability checks via `cap_console_write_gate(...)`.

- The gate requires `CAP_CONSOLE_WRITE` before success.
- Default path denies with `CAP_ERR_MISSING` until explicit grant.
- Revoke restores deny behavior immediately.
- Invalid subjects are rejected with `CAP_ERR_SUBJECT_INVALID`.

Validation command:

- `./build/scripts/test.sh capability_gate`

## Follow-on

- CAP-004 is complete: capability allow/deny markers are integrated in broader harness flows and validation bundles.
- CAP-005 is complete: see `docs/adr/0001-capability-core-boundary.md` for the architecture decision record.
- CAP-006 is complete: table internals now use packed bitsets while preserving external API semantics.
- CAP-007 is complete: serial-write capability boundary and dual-cap isolation tests are merged.
- CAP-008 is complete: debug-exit authorization is gated behind explicit capability checks and marker-backed validation.
- CAP-009 is complete: capability checks now write deterministic audit events (subject, capability, result) into a bounded ring buffer for test-time visibility without changing allow/deny semantics.
- CAP-010 is complete: grant/revoke mutation operations are now emitted to the same bounded audit stream with explicit operation type metadata, and mixed-flow ordering is validated deterministically in tests.
