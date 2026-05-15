# SecureOS Syscall ABI (`os_*`)

User-visible API surface declared in `user/include/secureos_api.h`. All entry
points return an `os_status_t` value. The kernel-side implementation lives in
the user-app native bridge (`kernel/user/process.c::app_native_*`) and behind
the FS / network / console services.

## Result codes

`os_status_t` (append-only):

| Name                | Value | Meaning                                                       |
| ------------------- | ----- | ------------------------------------------------------------- |
| `OS_STATUS_OK`      | 0     | Operation completed successfully.                              |
| `OS_STATUS_DENIED`  | 1     | Caller lacks the required capability or service-level grant.   |
| `OS_STATUS_NOT_FOUND` | 2   | Target resource (path, handle, env key, etc.) does not exist.  |
| `OS_STATUS_ERROR`   | 3     | Generic error (malformed args, I/O failure, internal error).   |

Numeric values are part of the ABI and must not change. New codes are appended
with strictly larger numeric values.

## Syscall surface

Each row lists the user-visible signature, the capability the call requires
(returned as `OS_STATUS_DENIED` if missing), and a one-line semantics summary.

| Syscall | Required capability | Notes |
| --- | --- | --- |
| `os_status_t os_console_write(const char *message)` | `CAP_CONSOLE_WRITE` | Write a NUL-terminated string to the console service. Deny-by-default; granted via launcher manifest. |
| `os_status_t os_fs_list_root(char *out, unsigned int out_size)` | `CAP_FS_READ` | Enumerate root entries; `out` is filled with newline-separated names. `OS_STATUS_ERROR` if `out_size` is too small. |
| `os_status_t os_fs_list_dir(const char *path, char *out, unsigned int out_size)` | `CAP_FS_READ` | Enumerate entries in `path`. |
| `os_status_t os_fs_read_file(const char *path, char *out, unsigned int out_size)` | `CAP_FS_READ` | Read entire file into `out`. |
| `os_status_t os_fs_write_file(const char *path, const char *content, int append)` | `CAP_FS_WRITE` | Write or append `content` to `path`. |
| `os_status_t os_fs_mkdir(const char *path)` | `CAP_FS_WRITE` | Create directory; idempotent on existing dir is implementation-defined (currently `OS_STATUS_ERROR`). |
| `os_status_t os_process_chdir(const char *path)` | none (process-local) | Change current working directory of the calling app. |
| `os_status_t os_process_getcwd(char *out, unsigned int out_size)` | none | Read current working directory. |
| `os_status_t os_env_get(const char *key, char *out, unsigned int out_size)` | none (process-local env) | Read process env var. |
| `os_status_t os_env_set(const char *key, const char *value)` | none | Set process env var. |
| `os_status_t os_env_list(char *out, unsigned int out_size)` | none | Newline-separated `KEY=VALUE` dump. |
| `os_status_t os_lib_list(char *out, unsigned int out_size)` | `CAP_FS_READ` (to enumerate library paths) | Enumerate currently-loaded libraries. |
| `os_status_t os_lib_load(const char *path, char *out, unsigned int out_size)` | `CAP_APP_EXEC` | Load a SOF-wrapped library; signature must verify (see `manifest.md`). Returns handle id in `out`. |
| `os_status_t os_lib_unload(unsigned int handle)` | `CAP_APP_EXEC` | Release a library previously loaded by `os_lib_load`. |
| `os_status_t os_net_device_ready(void)` | `CAP_NETWORK` | Returns `OS_STATUS_OK` once the NIC backend is live. |
| `os_status_t os_net_device_backend(char *out, unsigned int out_size)` | `CAP_NETWORK` | Backend descriptor string (e.g. `virtio-net-pci`). |
| `os_status_t os_net_device_get_mac(unsigned char *out, unsigned int out_size)` | `CAP_NETWORK` | 6-byte MAC into `out`; `out_size` must be ≥ 6. |
| `os_status_t os_net_frame_send(const unsigned char *frame, unsigned int len)` | `CAP_NETWORK` | Send a single L2 Ethernet frame. |
| `os_status_t os_net_frame_recv(unsigned char *out, unsigned int out_size, unsigned int *out_len)` | `CAP_NETWORK` | Non-blocking receive; `*out_len = 0` if no frame is queued. |
| `os_status_t os_net_ifconfig(char *out, unsigned int out_size)` | `CAP_NETWORK` | Human-readable interface summary. |
| `os_status_t os_net_http_get(const char *url, char *out, unsigned int out_size)` | `CAP_NETWORK` | HTTP/1.1 GET; v1 stack only. |
| `os_status_t os_net_https_get(const char *url, char *out, unsigned int out_size)` | `CAP_NETWORK` | HTTPS (TLS 1.2 via BearSSL) — `CAP_NETWORK` is sufficient, no separate TLS capability. |
| `os_status_t os_net_ping(const char *host, char *out, unsigned int out_size)` | `CAP_NETWORK` | ICMP echo. |
| `os_status_t os_apps_list(char *out, unsigned int out_size)` | `CAP_FS_READ` | Enumerate installed apps. |
| `os_status_t os_storage_info(char *out, unsigned int out_size)` | `CAP_FS_READ` | Storage backend summary. |
| `os_status_t os_get_args(char *out, unsigned int out_size)` | none (process-local) | Read the launcher-supplied argument string. |

## Calling convention

User apps are loaded by `kernel/user/process.c` and reach the kernel via the
`app_native_bridge_t` function-pointer table at the well-known address
`APP_NATIVE_BRIDGE_ADDR`. The bridge layout is currently considered an
**internal** detail of the static-link app model and is *not* yet a stable
ABI; SDK consumers must depend on the `os_*` symbols from
`user/include/secureos_api.h` and the matching `user/lib/` runtime, not on
the bridge offsets directly.

When the syscall surface migrates to a true syscall instruction (planned for
M6 SDK work, see `BUILD_ROADMAP.md` §5.6 + issue #136), the `os_*` signatures
remain stable; only the lowering changes.

## Capability check semantics

Every `OS_STATUS_DENIED` return corresponds to exactly one `cap_check()` call
and produces exactly one `CAP_AUDIT_OP_CHECK` audit event with
`result = CAP_ERR_MISSING` (see `capabilities.md`). There is no silent denial
path — if a syscall returns `OS_STATUS_DENIED`, the audit ring will contain a
matching event.

## Adding a new syscall

1. Append the prototype to `user/include/secureos_api.h` (do not reorder).
2. Append a row to the table above and state the required capability.
3. If a new capability is needed, follow the procedure in `capabilities.md`
   ("Adding a capability ID") *first*.
4. Update `Last verified against commit` below.

Last verified against commit: `9f4f7cc` (2026-05-15).
