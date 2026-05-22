# SecureOS Capability ABI

This document is the ABI-level summary of the capability subsystem. For
implementation history and per-CAP-NNN deltas, see
[`docs/architecture/CAPABILITIES.md`](../architecture/CAPABILITIES.md).

## Capability IDs

`capability_id_t` is a stable, append-only enum
(`kernel/cap/capability.h`):

| ID | Name | Gates |
| -- | ---- | ----- |
| 1  | `CAP_CONSOLE_WRITE` | `os_console_write`, `cap_console_write_gate` |
| 2  | `CAP_SERIAL_WRITE` | `cap_serial_write_gate` |
| 3  | `CAP_DEBUG_EXIT` | `cap_debug_exit_gate` |
| 4  | `CAP_CAPABILITY_ADMIN` | grant/revoke mutation actor authority |
| 5  | `CAP_DISK_IO_REQUEST` | raw block I/O |
| 6  | `CAP_FS_READ` | `os_fs_list_*`, `os_fs_read_file` |
| 7  | `CAP_FS_WRITE` | `os_fs_write_file`, `os_fs_mkdir` |
| 8  | `CAP_EVENT_SUBSCRIBE` | event-bus subscribe |
| 9  | `CAP_EVENT_PUBLISH` | event-bus publish |
| 10 | `CAP_APP_EXEC` | dynamic library load / app launch |
| 11 | `CAP_CODESIGN_BYPASS` | sealed-build only; never granted in prod |
| 12 | `CAP_NETWORK` | all `os_net_*` (incl. HTTPS) |
| 13 | `CAP_IPC_SEND` | `ipc_send` / `ipc_call` send leg (M1 sync IPC) |
| 14 | `CAP_IPC_RECV` | `ipc_recv` (M1 sync IPC) |
| 15 | `CAP_SYSCALL` | M1 syscall entry stub (reserved; see [syscalls.md](syscalls.md)). |

Rules (all enforced today):

- IDs are **append-only** once merged. Numeric IDs are never reused or
  renumbered.
- Unknown capability IDs **must** return `CAP_ERR_CAP_INVALID` from
  `cap_check` and from grant/revoke. They are never treated as success.
- Unknown subject IDs **must** return `CAP_ERR_SUBJECT_INVALID`. The maximum
  subject is `CAP_TABLE_MAX_SUBJECTS = 8` at `OS_ABI_VERSION=0`.

## Handle representation

A "capability handle" at this stage of the project is the pair
`(cap_subject_id_t, capability_id_t)` stored in the per-subject packed
bitset table (`kernel/cap/cap_table.{h,c}`). There is no separate
opaque-handle type yet; that is intentional until the broker slice (#85)
lands and demands sharable, revocable references.

When the broker slice ships, the handle type will:

- be opaque to user-space (no kernel pointer dereference required),
- carry the originating subject so revocation is total,
- be observable in audit events under their existing `subject_id` /
  `capability_id` fields plus a new handle-id column.

Until then, code that needs to refer to a "grant" refers to the
`(subject, capability)` pair directly.

## Result codes

`cap_result_t`:

| Value | Name | Meaning |
| ----- | ---- | ------- |
| 0 | `CAP_OK` | Explicit grant present. |
| 1 | `CAP_ERR_MISSING` | Subject is valid, capability is valid, no grant. |
| 2 | `CAP_ERR_SUBJECT_INVALID` | Subject out of range / unknown. |
| 3 | `CAP_ERR_CAP_INVALID` | Capability ID out of range / unknown. |

Syscall-layer wrappers translate `CAP_OK → OS_STATUS_OK` and any other
`cap_result_t` into `OS_STATUS_DENIED`. Calls that *can* succeed without
the capability but degrade (none today) must document that explicitly.

## Grant / revoke semantics

- All mutation goes through `cap_grant_as_for_tests` /
  `cap_revoke_as_for_tests` (test names — the runtime path is the same code
  behind a sealed entry). Every mutation requires an `actor_subject_id`
  that itself holds `CAP_CAPABILITY_ADMIN`.
- `CAP_CAPABILITY_ADMIN` is **non-delegable** (CAP-014): only the bootstrap
  root subject `0` may hold it, and attempts to grant it to any other
  subject return `CAP_ERR_MISSING` against the actor.
- Revocation is immediate. The next `cap_check` after a revoke must return
  `CAP_ERR_MISSING`.
- There is no "expiry" or "TTL" on a grant at `OS_ABI_VERSION=0`.

## Audit and sequence guarantees

The capability audit ring (`cap_audit_event_t`) is part of the ABI for
validators and the broker:

- Every check, grant, and revoke emits an event with a strictly monotonic
  `sequence_id` (CAP-016). The sequence never repeats, even across ring
  wrap.
- Mutation events include `actor_subject_id` (CAP-015).
- The ring has a fixed retained window (`CAP_AUDIT_EVENT_MAX = 32`) with
  FIFO eviction and a separately tracked `dropped` counter (CAP-011).
- Periodic checkpoints (`CAP_AUDIT_CHECKPOINT_INTERVAL = 8`) summarize each
  window with a tamper-evident `seal` (CAP-017/018). Validators enforce a
  strict schema for the checkpoint summary in CI (CAP-012/018).
- CAP-019 (sequence-window attestation) is in progress; it will tie the
  retained event window to the checkpoint window continuity invariant.

Consumers may read `cap_audit_get_for_tests` / `cap_audit_count_for_tests`
to walk the retained ring in test/validator contexts. In production the
broker (#85) will be the single consumer.

Last verified against commit: 9f4f7ccbb19c9ffb28ee4b6de2f3e93c35e65785
