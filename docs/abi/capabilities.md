# SecureOS Capability ABI

This is the SDK-facing reference for capability identifiers, the handle
representation, and the grant / revoke / audit semantics. The narrative
history (CAP-001 .. CAP-019) lives in
[`docs/architecture/CAPABILITIES.md`](../architecture/CAPABILITIES.md); the
file you are reading is the abridged contract that external code may rely on.

## Capability IDs

Defined in `kernel/cap/capability.h::capability_id_t`. **Append-only.** Numeric
values are stable across all `OS_ABI_VERSION` bumps.

| ID | Name                    | Guards                                    |
| -- | ----------------------- | ----------------------------------------- |
| 1  | `CAP_CONSOLE_WRITE`     | `os_console_write` and the console service. |
| 2  | `CAP_SERIAL_WRITE`      | Direct serial port writes.                |
| 3  | `CAP_DEBUG_EXIT`        | QEMU debug-exit signaling.                |
| 4  | `CAP_CAPABILITY_ADMIN`  | Mutating the cap table (`cap_grant_as` / `cap_revoke_as`). Non-delegable; bootstrap subject `0` only. |
| 5  | `CAP_DISK_IO_REQUEST`   | Raw disk I/O requests.                    |
| 6  | `CAP_FS_READ`           | All `os_fs_*` read paths.                 |
| 7  | `CAP_FS_WRITE`          | `os_fs_write_file`, `os_fs_mkdir`.        |
| 8  | `CAP_EVENT_SUBSCRIBE`   | Event bus subscriptions.                  |
| 9  | `CAP_EVENT_PUBLISH`     | Event bus publication.                    |
| 10 | `CAP_APP_EXEC`          | Loading / running additional apps and libraries. |
| 11 | `CAP_CODESIGN_BYPASS`   | Loading an unsigned bundle. **Strongly restricted; bootstrap-only.** |
| 12 | `CAP_NETWORK`           | All `os_net_*` calls (HTTP and HTTPS).    |

Result codes (`cap_result_t`, append-only):

| Name                       | Value | Meaning                                       |
| -------------------------- | ----- | --------------------------------------------- |
| `CAP_OK`                   | 0     | Subject has explicit grant.                    |
| `CAP_ERR_MISSING`          | 1     | Subject is valid; capability is not granted.   |
| `CAP_ERR_SUBJECT_INVALID`  | 2     | Subject identifier is out of bounds / unknown. |
| `CAP_ERR_CAP_INVALID`      | 3     | Capability identifier is out of bounds / unknown. |

## Handle representation

A capability "handle" in current SecureOS is **a per-subject grant entry in
the kernel capability table**, *not* a userspace-passable opaque token. There
are no `cap_handle_t` values to round-trip across the syscall boundary.

- The cap table is fixed-capacity: `CAP_TABLE_MAX_SUBJECTS = 8` per-subject
  slots, each storing a packed bitset of granted capability IDs (CAP-002 /
  CAP-006).
- A "subject" is identified by `cap_subject_id_t` (`uint32_t`). Subject `0`
  is the bootstrap root and is the only subject permitted to hold
  `CAP_CAPABILITY_ADMIN` (CAP-014).
- All capability checks happen kernel-side via `cap_check(subject, cap_id)`;
  user space never sees the bitset.

A future SDK iteration may introduce explicit handle objects (e.g. a
delegable read-only file handle) per `BUILD_ROADMAP.md` §6.1. Until that
lands, the only way for an app to "have" a capability is for it to be granted
to its subject id at launch time via the manifest (see `manifest.md`).

## Grant / revoke semantics

- **Default state is deny.** No subject holds any capability after
  `cap_table_init` until an explicit grant.
- **Mutation requires `CAP_CAPABILITY_ADMIN`.** `cap_grant_as` /
  `cap_revoke_as` reject the call (`CAP_ERR_MISSING` audited against the
  actor) when the actor lacks `CAP_CAPABILITY_ADMIN` (CAP-013).
- **Non-delegable admin.** `CAP_CAPABILITY_ADMIN` itself can only be granted
  to subject `0`; any attempt to grant it to another subject fails (CAP-014).
- **Revoke is immediate.** A `cap_check` returns `CAP_ERR_MISSING` on the very
  next call after a successful revoke; there is no caching layer or grace
  period (CAP-003).
- **Invalid inputs never silently succeed.** Unknown subject or capability
  IDs return the matching `CAP_ERR_*_INVALID` and are recorded in the audit
  ring with that error code.

## Audit and observability

Every `cap_check` / `cap_grant` / `cap_revoke` produces exactly one
`cap_audit_event_t`:

```c
typedef struct {
  uint64_t            sequence_id;       /* monotonic; preserved across overflow */
  cap_audit_op_t      operation;         /* CHECK | GRANT | REVOKE */
  cap_subject_id_t    actor_subject_id;  /* who attempted the mutation */
  cap_subject_id_t    subject_id;        /* who the result applies to */
  capability_id_t     capability_id;
  cap_result_t        result;
} cap_audit_event_t;
```

Guarantees (CAP-009 .. CAP-019):

- The audit ring has bounded depth (`CAP_AUDIT_EVENT_MAX = 32`) with explicit
  FIFO retention; a monotonic dropped-events counter is exposed via
  `cap_audit_dropped_for_tests()`.
- `sequence_id` is monotonically increasing across the lifetime of the
  process and survives ring wrap.
- Tamper-evident checkpoints (`cap_audit_checkpoint_t`, every
  `CAP_AUDIT_CHECKPOINT_INTERVAL = 8` events) seal a window of sequence ids
  and are exposed via `cap_audit_checkpoint_get_for_tests()`.
- Logging never alters allow / deny semantics. The act of writing an audit
  event must not change the result returned to the caller (CAP-009 +
  test-suite invariant).

The validator JSON summary (see `BUILD_ROADMAP.md` §4.3 / issue #110)
surfaces the same sequence-id and checkpoint metadata so that CI can assert
on capability-policy continuity without parsing free-text logs.

## Adding a capability ID

1. Append the new identifier to `capability_id_t` in
   `kernel/cap/capability.h`. **Do not** renumber existing entries.
2. Append a row to the table above with the guarded surface.
3. Update `docs/architecture/CAPABILITIES.md` with a CAP-NNN narrative entry.
4. Add at least one allow-path and one deny-path test exercising the new
   capability (see `tests/` and the relevant validator script).
5. Bump `Last verified against commit` below.

Last verified against commit: `9f4f7cc` (2026-05-15).
