# SecureOS Capability ABI

> **Owner:** kernel / capability subsystem
> **Status:** draft `v0` — append-only enum, handle layer in progress (#233)
> **Last reviewed:** 2026-05-22
> **Applies to:** `OS_ABI_VERSION = 0`
> **Tracking issues:** [#150](https://github.com/rwrife/SecureOS/issues/150), [#163](https://github.com/rwrife/SecureOS/issues/163), [#233](https://github.com/rwrife/SecureOS/issues/233)

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
| 16 | `CAP_CLOCK_SET` | `clock_service_set_time` (system clock mutation). |
| 17 | `CAP_INPUT_MOUSE` | PS/2 mouse byte queue + `mouse_hal_*` consumers (renamed from `CAP_MOUSE`; numeric id unchanged). Enforcement follow-up: #349. |
| 18 | `CAP_GFX_FRAMEBUFFER` | per-session virtual framebuffer + VGA front-buffer writes (window-manager / `vfb_font` path). Enforcement follow-up: #349. |
| 19 | `CAP_INPUT_KEYBOARD` | PS/2 keyboard byte queue + keyboard HAL consumers. Enforcement follow-up: #349. |

Rules (all enforced today):

- IDs are **append-only** once merged. Numeric IDs are never reused or
  renumbered.
- Unknown capability IDs **must** return `CAP_ERR_CAP_INVALID` from
  `cap_check` and from grant/revoke. They are never treated as success.
- Unknown subject IDs **must** return `CAP_ERR_SUBJECT_INVALID`. The maximum
  subject is `CAP_TABLE_MAX_SUBJECTS = 8` at `OS_ABI_VERSION=0`.

## Handle representation

### `(subject, capability)` pair (legacy `cap_table` API)

The original capability surface stores the pair
`(cap_subject_id_t, capability_id_t)` directly in the per-subject packed
bitset table (`kernel/cap/cap_table.{h,c}`). This API remains the single
call path for every kernel call site at M1 (`cap_check`,
`cap_table_grant`, `cap_table_revoke`).

### 32-bit packed handle (`cap_handle_t`, M1-CAPTBL-002, #233)

A wire-stable 32-bit identifier lives alongside the legacy API in
`kernel/cap/cap_handle.{h,c}`. It is the value that the M1 synchronous IPC
primitive (#180 / #185) and the future syscall-entry trampoline (#192) will
pass across the kernel/user boundary. Layout is **frozen at
`OS_ABI_VERSION=0`** — a `#error` in `cap_handle.h` fires on any bump,
and any change to the field widths below is a v0→v1 ABI break.

| Bits  | Width | Field        | Meaning                                                           |
| ----- | ----- | ------------ | ----------------------------------------------------------------- |
| 0..15 | 16    | `slot`       | Index into the global `cap_handle_row` table (`0 .. CAP_HANDLE_TABLE_MAX-1`, currently 64). |
| 16..29| 14    | `generation` | Low 14 bits of the row's monotonic generation counter; bumped on every revoke. |
| 30..31| 2     | `tag`        | `0b00` = null/invalid, **`0b01` = kernel capability handle**, `0b10`/`0b11` reserved (future file/IPC/broker kinds). |

Validation contract (`cap_gate_check_handle` / `cap_gate_check_handle_result`):

- `tag != 0b01`            → `CAP_ERR_CAP_INVALID`
- `slot >= CAP_HANDLE_TABLE_MAX` → `CAP_ERR_CAP_INVALID`
- row not live              → `CAP_ERR_MISSING`
- generation mismatch       → `CAP_ERR_MISSING` (stale handle after revoke)
- row's `cap_id != expected_cap` → `CAP_ERR_CAP_INVALID`

`cap_handle_grant(subject, cap)` returns a handle for the live row, or
`CAP_HANDLE_NULL` on table full / bad inputs. Grants are idempotent: a
second grant for the same live `(subject, cap)` returns the **same**
handle. `cap_handle_revoke(handle)` bumps the row's generation, so the
same numeric value denies on the next gate; the row is retained so the
generation counter remains observable indefinitely.

M1-CAPTBL-002 ships the representation and the gate-check entry point.
M1-CAPTBL-003 (#239) adds `cap_handle_revoke_subject(owner)` — a single
pass over the global table that transitions every live row owned by
`owner` to REVOKED and bumps its generation, so every previously-issued
handle for those rows now fails the gate with `CAP_ERR_MISSING`. The
`kernel/proc/process.c::process_destroy` path calls this hook on every
successful destroy, making process exit the authoritative "all handles
for this subject are stale" trigger. Bulk revoke is best-effort by
contract (no error code; bad subject ids return `0` revoked).
Subtree revoke (`cap_handle_revoke_subtree`) is M1-CAPTBL-004 (#241):
the symbol is declared in `kernel/cap/cap_handle.h` and **reserved at
v0** so the M5 ownership-graph cascading-deletion work (#118) and the
M4 broker subtree-revoke (#115) have a stable kernel symbol to compile
against. In v0 the implementation is a deliberate stub that
**unconditionally returns `CAP_ERR_CAP_INVALID` with zero side effects**
— there is no graph to walk (the `parent_handle` field on
`cap_handle_row` is reserved-and-zero today). Callers MUST NOT depend
on subtree-revoke semantics until #118 lands the real walk. Façade
migration with byte-exact audit parity is -005, and IPC integration is
-006 — each landing as its own PR.

### Legacy notes

Until the façade migration (M1-CAPTBL-005) lands, the legacy `cap_table`
API and the new `cap_handle` API maintain **independent** state. Existing
call sites continue to use `cap_check` / `cap_table_grant` unchanged.
When the broker slice (#85) reifies handle ownership across processes, it
will consume the same `cap_handle_t` defined here.

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
- The canonical capability-mutation audit marker families (`cap.grant` /
  `cap.revoke`, serialized as `CAP_AUDIT:...:op=GRANT|REVOKE:...`) are
  indexed in [`audit-markers.md`](audit-markers.md) §4.1–§4.2, including
  the v0 `resource` rule (`-`) and callsite-derived `cause` taxonomy.
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

### Byte-exact audit fixture-diff (M1-CAPTBL-005)

The textual serialization above is wire ABI. A representative M2 console
sequence (grant, allow check, deny check, revoke, post-revoke deny,
second-cap grant, re-grant, plus the three invalid-subject / one
invalid-cap deny-class paths) is pinned byte-for-byte in
`tests/capability_audit_fixture_test.c` and run as the
`capability_audit_fixture` validator target. Any future migration of
`kernel/cap/cap_table.{c,h}` onto the handle layer
(`kernel/cap/cap_handle.{c,h}`) MUST preserve every byte of that
fixture or fail this test loudly with a per-line diff. Updating the
fixture is allowed only under an explicit audit-ABI bump per
BUILD_ROADMAP §7.

Last verified against commit: 6305aea4f0eaee1547ed0e4c2be022688af3c9cf
