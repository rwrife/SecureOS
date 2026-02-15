# SecureOS Capabilities (M1 bootstrap)

## CAP-001 contract

SecureOS capability checks are deny-by-default and produce explicit result codes.

### Capability IDs

Current stable IDs:

- `CAP_CONSOLE_WRITE = 1`

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

Current implementation stores `CAP_CONSOLE_WRITE` grants in an internal array while keeping the table interface stable for additional capability IDs.

## Follow-on

- CAP-003: gate first privileged operation (console write) behind capability checks.
- Extension path: move table internals from per-cap arrays to packed bitsets as capability IDs grow, without changing external API semantics.
