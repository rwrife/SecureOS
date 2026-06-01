# SecureOS Syscall / User API Surface

> **Owner:** kernel / user-API
> **Status:** draft `v0` — surface still iterating
> **Last reviewed:** 2026-05-29
> **Applies to:** `OS_ABI_VERSION = 0`
> **Tracking issue:** [#93](https://github.com/rwrife/SecureOS/issues/93)

Adding new calls is allowed; signatures of listed calls are
append-only-with-care (see [versioning.md](versioning.md)).

User-space applications call into the kernel through the prototypes declared
in [`user/include/secureos_api.h`](../../user/include/secureos_api.h). On
real hardware these will eventually trap into the kernel; today many are
serviced through the native bridge stub (`user/runtime/secureos_api_stubs.c`)
or wired directly to kernel services for in-kernel tests.

## Status / error model

All calls return `os_status_t`:

| Value | Name | Meaning |
| ----- | ---- | ------- |
| 0 | `OS_STATUS_OK` | Call succeeded; out-buffers are populated. |
| 1 | `OS_STATUS_DENIED` | Caller lacks the required capability (or admin-gate failed). |
| 2 | `OS_STATUS_NOT_FOUND` | Target resource (file, env key, lib, etc.) does not exist. |
| 3 | `OS_STATUS_ERROR` | Generic failure (bad args, out-of-space, transport error). |

`OS_STATUS_DENIED` is the only success-shaped result an unprivileged caller
should ever observe for a missing capability — it is **never** silently
treated as success and is **never** collapsed into `OS_STATUS_ERROR`. This
guarantee is what makes zero-trust regression tests deterministic.

Buffer-returning calls take `(out_buffer, out_buffer_size)`; truncation is
reported as `OS_STATUS_ERROR` and the contents of `out_buffer` are
unspecified in that case.

## Surface (current)

### Console

| Call | Required cap | Notes |
| ---- | ------------ | ----- |
| `os_console_write(message)` | `CAP_CONSOLE_WRITE` | Goes through `cap_console_write_gate` in the kernel. Apps must be routed through the launcher (see [manifest.md](manifest.md)); direct subjects without an explicit grant get `OS_STATUS_DENIED`. |

### Filesystem

| Call | Required cap | Notes |
| ---- | ------------ | ----- |
| `os_fs_list_root(out, len)` | `CAP_FS_READ` | |
| `os_fs_list_dir(path, out, len)` | `CAP_FS_READ` | |
| `os_fs_read_file(path, out, len)` | `CAP_FS_READ` | |
| `os_fs_write_file(path, content, append)` | `CAP_FS_WRITE` | `append == 0` truncates, non-zero appends. |
| `os_fs_mkdir(path)` | `CAP_FS_WRITE` | |

### Process / environment

| Call | Required cap | Notes |
| ---- | ------------ | ----- |
| `os_process_chdir(path)` | (none — bound to caller) | |
| `os_process_getcwd(out, len)` | (none) | |
| `os_process_exit(status)` | (none — bound to caller) | M7-TOOLCHAIN-003 (#406). Terminates the calling process. Does not return when a real bridge is attached. The host-build wrapper reaches through the fixed bridge address (`SECUREOS_NATIVE_BRIDGE_ADDR`) like every other bridge-mediated call, so it is not safe to invoke on bare host without a mapped bridge — host validation is link-time only (`tests/process_exit_wrapper_test.c`). The `status` value is currently advisory (richer exit-code surface is a follow-up). |
| `os_process_spawn(path, argv, flags, *out_exit_status)` | `CAP_APP_EXEC` | M7-TOOLCHAIN-003 slice 2 (#422). Synchronous spawn of a staged SOF binary through the existing launcher (`process_run`) — no new trust path, same codesign + capability gate the console uses. Argv marshalling: `argv` is a NULL-terminated array of NUL-terminated strings (POSIX `execv` shape); the wrapper space-joins `argv[1..]` into the single `raw_args` string consumed by the launcher (no length-prefixed wire format on this slice). `flags` is reserved and MUST be 0; non-zero returns `OS_STATUS_ERROR`. On `OS_STATUS_OK` the child's `os_process_exit` status (captured by the launcher's fault-recovery slot into `g_native_exit_status`) is written into `*out_exit_status` when the pointer is non-NULL. Deny path (missing `CAP_APP_EXEC`) returns `OS_STATUS_DENIED` and emits a canonical `CAP:DENY:<subject>:app_exec:<resource>` audit marker before the filesystem is touched (parity with `proc_emit_table_full_deny_marker` in `kernel/proc/process.c`). Bridge version bump 2 → 3 only; no `OS_ABI_VERSION` bump. Env marshalling is deferred to a follow-up slice. Host validation is link-time only (`tests/process_spawn_wrapper_test.c`). |
| `os_mem_brk(delta, out_prev_break)` | (none — bound to caller) | M7-TOOLCHAIN-001 slice 2 (#421). POSIX `sbrk(2)`-shape heap extension for the calling process. On success writes the *previous* break (the address of the first freshly-committed byte on positive `delta`) through `out_prev_break` and returns `OS_STATUS_OK`. Out-of-arena growth returns `OS_STATUS_DENIED` and does NOT move the break or panic the kernel — the deny-clean contract that lets the freestanding `user/libs/clib` allocator fail the originating `malloc`/`realloc` call without crashing. `out_prev_break == NULL` returns `OS_STATUS_ERROR` without touching the bridge (the host fall-through the link-pin test exercises). Bridge slot `mem_brk` (bridge version 4+). Per-process pool resets when the launcher tears down the top-level bridge so a fresh app starts at break=0. Host validation: `tests/mem_brk_wrapper_test.c` (signature + symbol + NULL-out guard); end-to-end round-trip on the live arena (grow / shrink / over-cap-deny / arena-reset) is exercised by the `_qemu`-tier peer `tests/mem_brk_qemu_test.c` introduced by #495. |
| `os_env_get/set/list(...)` | (none) | Per-process env. |

### Libraries

| Call | Required cap | Notes |
| ---- | ------------ | ----- |
| `os_lib_list(out, len)` | (none) | Discovery only. |
| `os_lib_load(path, out, len)` | `CAP_APP_EXEC` | Loads a shared library into the caller's process image. |
| `os_lib_unload(handle)` | `CAP_APP_EXEC` | |

### Network (M1 surface, see #79)

| Call | Required cap | Notes |
| ---- | ------------ | ----- |
| `os_net_device_ready()` | `CAP_NETWORK` | |
| `os_net_device_backend(out, len)` | `CAP_NETWORK` | |
| `os_net_device_get_mac(out, len)` | `CAP_NETWORK` | |
| `os_net_frame_send(frame, len)` | `CAP_NETWORK` | Raw L2 send. |
| `os_net_frame_recv(out, len, *out_len)` | `CAP_NETWORK` | Raw L2 recv. |
| `os_net_ifconfig(out, len)` | `CAP_NETWORK` | Diagnostic. |
| `os_net_http_get(url, out, len)` | `CAP_NETWORK` | Convenience HTTP/1.1 client. Subject to URL-scheme gate (#79). |
| `os_net_https_get(url, out, len)` | `CAP_NETWORK` | TLS 1.2 via BearSSL in user-space; no separate capability. |
| `os_net_ping(host, out, len)` | `CAP_NETWORK` | ICMP via netlib. |

`CAP_NETWORK` is a single coarse capability today; finer-grained network
policy (per-host, per-scheme) is tracked under #79 and lives inside netlib /
the dispatch shim, not as new capability IDs at this layer.

### Apps / system

| Call | Required cap | Notes |
| ---- | ------------ | ----- |
| `os_apps_list(out, len)` | (none) | Discovery. |
| `os_storage_info(out, len)` | (none) | Read-only. |
| `os_get_args(out, len)` | (none) | Caller's argv. |

## Reserved syscall vector range (M1)

Issue #232 (plan #198) reserves a frozen syscall vector range under
`OS_ABI_VERSION = 0` so the ABI slot exists before any real M2+ caller
binds to it. The single source of truth is
[`kernel/proc/syscall_entry.h`](../../kernel/proc/syscall_entry.h):

- Range: `[SYSCALL_VECTOR_BASE, SYSCALL_VECTOR_BASE + SYSCALL_VECTOR_COUNT)`
  = `[0x0000, 0x0010)` (16 slots).
- Contract in M1: `kernel_syscall_entry()` returns `IPC_ERR_INVALID_MSG`
  for **every** vector — in-range or out-of-range — and emits a
  canonical `CAP:DENY:<actor>:syscall:-` marker via the shared
  [`cap_deny_marker`](capability-deny-contract.md) formatter so the
  deny-marker contract tests apply uniformly the moment a real caller
  is wired.
- ABI anchor: `SYSCALL_ENTRY_ABI_ANCHOR = (OS_ABI_VERSION << 16) |
  SYSCALL_VECTOR_COUNT`. Cross-checked by
  `tests/syscall_entry_stub_test.c` so the reservation cannot silently
  drift away from the `OS_ABI_VERSION` anchor (same pattern as #228 for
  manifest `os_abi_version`).
- Gating capability: `CAP_SYSCALL = 15` (declared in
  `kernel/cap/capability.h`, append-only enum slot).

Vectors inside the range are reserved but unbound; renumbering or
reusing one for a different purpose once a real M2+ caller binds is a
covered ABI break under [versioning.md](versioning.md). Widening the
range (raising `SYSCALL_VECTOR_COUNT`) is additive and allowed within
`OS_ABI_VERSION = 0`.

No GDT/TSS/ring-3 plumbing exists in M1 — see plan #198 ("stubbed but
unused"). The reservation is purely an ABI-shape anchor.

## Adding a syscall

1. Declare the prototype in `user/include/secureos_api.h`.
2. Add the in-kernel implementation behind the appropriate `cap_*_gate` (or
   add a new gate if there is no capability covering it — and update
   [capabilities.md](capabilities.md)).
3. If the call is sensitive, add a launcher manifest field that controls the
   grant (see [manifest.md](manifest.md)) rather than auto-granting to the
   bootstrap subject.
4. Add an allow-path and a deny-path test under `build/scripts/test_*.sh`.
5. Update this table and bump the verification line below.

Last verified against commit: 313621e

