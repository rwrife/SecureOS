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

## Follow-on

- CAP-002: dedicated per-subject table abstraction with explicit grant/revoke flows.
- CAP-003: gate first privileged operation (console write) behind capability checks.
