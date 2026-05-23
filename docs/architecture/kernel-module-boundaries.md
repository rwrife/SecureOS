# Kernel / Module / User-Lib Boundary Conventions

Status: accepted (M1/M2 baseline)
Scope: BUILD_ROADMAP.md §8 item 13. Companion to:
- `AGENTS.md` (project intent — "kernel should be extremely minimal")
- `docs/architecture/CAPABILITIES.md` (capability ID registry)
- `docs/abi/` (#93) — ABI surface
- `docs/architecture/CODING_CONVENTIONS.md` (style)

The purpose of this document is to give reviewers and agents a single page to
point to when a PR puts code in the wrong layer or links across a forbidden
boundary. It describes the tree we **have today**, not the aspirational tree in
`BUILD_ROADMAP.md` §2.2; drift between the two is called out at the end.

---

## 1. Layer model

SecureOS has four code layers. Each layer may include only from itself and from
layers strictly below it.

```
apps/         user/apps/*           ← top, untrusted, dynamically loaded
  |
user libs/    user/libs/*           ← shared user-space functionality
  |
modules/      (not yet present as a top-level tree — see §5 drift)
  |
kernel        kernel/core, kernel/cap, kernel/sched, kernel/event, kernel/fs,
              kernel/user (process.c), kernel/format, kernel/crypto
  |
HAL           kernel/hal/*_hal.{c,h}
  |
arch          kernel/arch/*        ← bottom, CPU/board specific
```

Allowed include direction is **downward only**:

- `kernel/arch/**` includes nothing above arch.
- `kernel/hal/**` includes only `kernel/arch/**` and standard freestanding
  headers.
- `kernel/**` (core/cap/sched/event/fs/user/format/crypto) includes
  `kernel/hal/**` and `kernel/arch/**`, plus other `kernel/**` peers.
- `user/libs/**` includes only other `user/libs/**` peers and SDK headers
  (currently the ABI headers under `kernel/cap/` exposed for syscall numbers /
  capability IDs — these are intentionally read-only from user space).
- `user/apps/**` includes `user/libs/**` and SDK headers. Apps must **never**
  `#include` from `kernel/**` source paths.

Reverse direction is forbidden. The kernel must not `#include` anything from
`user/`; if the kernel needs to know a constant that user space also needs, the
constant lives in a header under `kernel/` (typically `kernel/cap/` or
`kernel/format/`) and user space includes that header through the ABI surface.

---

## 2. What belongs where

### `kernel/`

Kernel-resident code only. Per `AGENTS.md`, the kernel is responsible for, and
**limited to**:

1. Session management (`kernel/core/session_manager.{c,h}`).
2. Process launching from on-disk binaries (`kernel/user/launcher_exec.{c,h}` —
   the command/ELF launcher). The M1 process control block (PCB) and process
   abstraction live separately in `kernel/proc/process.{c,h}`.
3. The console (`kernel/core/console.{c,h}`).
4. The event bus (`kernel/event/**`).
5. The capability gatekeeper (`kernel/cap/**`).
6. HAL-backed drivers for hardware the kernel itself must touch
   (`kernel/hal/**` + `kernel/drivers/**`).
7. Filesystem service entry points (`kernel/fs/**`) — see §3 capability rule.
8. Binary format / loader plumbing (`kernel/format/**`) used by
   `kernel/user/launcher_exec.c`.
9. Cryptographic primitives required by the loader / signature verifier only
   (`kernel/crypto/**`). User-space crypto (TLS, etc.) lives in `user/libs/`.

If a feature does not appear in the list above, it does not belong in
`kernel/`. Examples that explicitly do **not** belong in the kernel: HTTP/TLS
stacks, shells, file viewers, package managers, network tools (`ifconfig`,
`ping`, `http`).

### `kernel/hal/`

Every hardware touch goes through a HAL. The HAL layer hides architecture and
virtualization differences. Today there is one HAL pair per device class:

- `serial_hal.{c,h}`
- `video_hal.{c,h}`
- `storage_hal.{c,h}`
- `network_hal.{c,h}`

Rule: if a kernel `.c` file reaches for an `outb`, `inb`, MMIO write, or a
device-specific register, it must do so through a HAL function. Adding a new
device means adding a new `<name>_hal.{c,h}` pair before any caller in
`kernel/core`, `kernel/fs`, `kernel/user`, or `kernel/drivers` uses it.

### `user/libs/`

Shared user-space functionality consumed by more than one app, built as
standalone artifacts (`artifacts/lib/*.lib`). Current libs:

- `envlib` — environment helpers.
- `fslib` — filesystem helpers over the fs service syscalls.
- `netlib` — network helpers (UDP/TCP/HTTP/HTTPS) on top of the kernel network
  syscalls; TLS lives here, not in the kernel.
- `soflib` — SOF (signed object format) helpers used by apps and tools.

Rule (from `AGENTS.md`): "any command that offers functionality that would be
useful to another application should pull the functionality out into a
standalone library." When two apps need the same logic, the first refactor is
to move it into `user/libs/`.

### `user/apps/`

User-facing binaries loaded by `process.c` at runtime. The kernel has **no**
build-time knowledge of these binaries; they are discovered by filename and
launched via the loader. Apps declare any required capabilities in their
manifest (see §3 below). Today:

- `os` — OS-level commands placed under `/os` on the disk image.
- `filedemo` — demo app.
- `vgahello` — demo app.

Any non-kernel-essential command (per `AGENTS.md`) belongs here, not in the
kernel.

---

## 3. The capability-gate rule

Every syscall / service entry that does privileged work **must** check a
capability **before** it does the work. The check belongs at the kernel-side
entry point — never inside the user library, never inside the app.

Concretely:

1. The capability ID lives in `docs/architecture/CAPABILITIES.md` and is
   defined as a numeric constant in the kernel (see #150 `OS_ABI_VERSION=0`
   anchor for header location convention).
2. The kernel-side service function (e.g. `fs_service_*`, `net_*`,
   `event_publish`, `process_spawn`) calls the capability check as its first
   non-trivial statement, returning a `CAP_ERR_*` code on denial.
3. The capability is granted to a process via the manifest at load time
   (`process.c` consumes the manifest, asks `kernel/cap/` to record the grant,
   and only then transfers control).
4. User libraries (`user/libs/`) must **not** attempt to gate or fake a
   capability check on the kernel's behalf. They invoke the syscall; the
   kernel decides.

If a syscall has no capability listed in `CAPABILITIES.md`, it cannot do
privileged work yet — file an issue rather than adding the call.

---

## 4. Do / don't, with examples from recent merges

### ✅ Do: route hardware through a HAL

`kernel/hal/serial_hal.c` is the only place that talks to the 16550 UART
registers. `console.c` calls `serial_hal_write_byte` and never touches `outb`
directly. New ports of the serial driver only have to land in `serial_hal.c`
plus a new `kernel/arch/<arch>/` translation.

### ✅ Do: gate the syscall, not the app

The `http` command is gated by `CAP_NETWORK`. The check lives in the kernel
network syscall path, not in `netlib` and not in `user/apps/os/http`. An app
without `CAP_NETWORK` in its manifest cannot bypass the gate by linking
`netlib` directly — the syscall still refuses.

### ✅ Do: keep crypto out of the kernel when it serves user-space

TLS (BearSSL, #117) lives in `user/libs/netlib`. The kernel exposes raw frame
I/O via `network_hal` and the network syscalls. The kernel `kernel/crypto/`
tree carries only what the loader / signature verifier needs (ed25519, #133).

### ✅ Do: load OS commands as standalone binaries

`user/apps/os/*` are built into `artifacts/os/*.bin`, copied to the disk
image, and launched by `process.c` from `/os/<name>.bin`. The kernel has no
compile-time reference to `ifconfig`, `ping`, or `http` — adding a new OS
command does not require a kernel rebuild.

### ❌ Don't: include kernel headers from user space

A `user/apps/foo/foo.c` adding `#include "kernel/fs/fs_service.h"` is a layer
violation. The right move is to call through `fslib` (and to add the helper to
`fslib` if it is missing).

### ❌ Don't: add a "fast path" that skips the capability check

A patch that calls the underlying service function directly to "avoid the
overhead" of the capability check is a hard reject. The check is the API.

### ❌ Don't: hardcode a device register in `kernel/core/` or `kernel/fs/`

If you find yourself writing `outb(0x3F8, ...)` outside `kernel/hal/`, stop
and add or extend a HAL function instead.

---

## 5. Known drift (explicitly not fixed here)

This doc describes the tree we have. The following items are tracked
separately and are intentionally **out of scope** for the boundary
conventions:

- No top-level `modules/` directory exists today; services that
  `BUILD_ROADMAP.md` §2.2 describes as modules currently live as
  `kernel/<subsystem>/` (kernel-resident) or `user/apps/os/<name>` (loaded as
  standalone binaries). Tree restructuring is a separate, large refactor and
  is not blocked by this doc.
- Plan directory drift (`plans/` vs `docs/plans/`) — tracked in #149.
- `.sh` ↔ `.ps1` script parity — tracked in #156.
- `validate_bundle.sh` orphan TEST_TARGETS — tracked in #129.
- Adding a CI linter / `include-what-you-use`-style enforcement of the
  layer rules above is a desirable follow-up but is **not** required by
  this doc; it can be filed as a separate issue.

When the drift items above are resolved, this doc should be revised in the
same PR so the "is" matches the "ought."

---

## 6. Cross-references

### M1 acceptance demo

The BUILD_ROADMAP §5.1 "two modules exchange message" and "unauthorized
operation denied with explicit error" bullets are realised by the
in-tree demo registered in `kernel/proc/module_registry.{c,h}` and
driven by `tests/m1_ipc_demo_test.c` (build/scripts/test_m1_ipc_demo.sh,
issue #251, plan #198 slice 4). Three compile-time modules—`m1-sender`,
`m1-receiver`, `m1-unauth`—are spawned via `proc_spawn_module` (which
chains `process_create` + `cap_table_grant` + `cap_handle_grant` +
`proc_sched_register`) and exchange a single envelope through the
handle-gated `ipc_send_h` / `ipc_recv_h` entry points. Allow path emits
`TEST:PASS:m1_ipc_allow`; deny path emits `TEST:PASS:m1_ipc_deny` plus
the canonical `CAP:DENY:<m1-unauth>:ipc_send:-` marker exactly once.

- `AGENTS.md` — project intent.
- `BUILD_ROADMAP.md` §2.2, §8 item 13 — aspirational tree and the requirement
  for this doc.
- `docs/architecture/CAPABILITIES.md` — capability ID registry (`CAP_*`).
- `docs/abi/` (#93) — ABI surface this doc sits one layer above.
- `docs/test-plans/` (#109) — links here from the registry as the canonical
  "where does this code go?" answer.
- #150 — `OS_ABI_VERSION=0` header convention.
