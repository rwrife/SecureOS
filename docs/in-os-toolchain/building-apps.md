# Building Apps Inside SecureOS

This document describes the **in-OS toolchain**: compiling a C source file
into a runnable SecureOS binary from *inside* a running SecureOS instance,
without the host Docker toolchain.

> **Status — phased rollout.** The user-facing layout (`/apps/dev`, the
> `hello.c` sample, this documentation) and the vendored compiler
> (`vendor/tinycc/`) land in **Phase 1**. The functional compiler (`cc`) and
> the runtime primitives it needs arrive across Phases 2–6. See
> [`plans/2026-05-28-in-os-toolchain-self-hosting.md`](../../plans/2026-05-28-in-os-toolchain-self-hosting.md)
> for the full plan and acceptance tests. Sections below marked *(planned)*
> describe behavior that is not yet wired.

## TL;DR (the target experience)

```
sosh> cc /apps/dev/hello.c -o /apps/hello.bin
sosh> hello
hello from inside SecureOS
```

For the full v0 compiler command-line contract (canonical invocation,
flag grammar, diagnostics, and capability surface), see
[`docs/in-os-toolchain/cc-cli.md`](./cc-cli.md)
(issue [#552](https://github.com/rwrife/SecureOS/issues/552)).

## Where the tools live

Everything for on-device development is under **`/apps/dev`** on the disk:

| Path                      | Purpose                                              |
|---------------------------|------------------------------------------------------|
| `/apps/dev/hello.c`       | Validation sample — compile it to test the toolchain |
| `/apps/dev/building.txt`  | On-device quick-start companion; keep in sync with this doc |
| `/apps/dev/cc` *(planned)*       | The in-OS C compiler (TinyCC-based)           |
| `/apps/dev/include/` *(planned)* | Public headers (`secureos_api.h`, `os/*.h`)   |
| `/apps/dev/lib/`                 | TinyCC linker/crt search root (ships placeholder README plus staged `libclib.a`/`libsofpack.a` when host artifacts exist) |
| `/apps/dev/tcc/`                 | TinyCC runtime helper root (`CONFIG_TCCDIR`; placeholder README plus staged `libtcc1.a` when host artifact exists) |

The repo source that gets staged to `/apps/dev` lives in the top-level
[`dev/`](../../dev) directory (analogous to how `scripts/` is staged to
`/scripts`). It is wired into the image by
[`build/scripts/build_disk_image.sh`](../../build/scripts/build_disk_image.sh).

When present in `artifacts/user/libs/`, the freestanding archives
`libclib.a` and `libsofpack.a` are staged to `/apps/dev/lib/` and become
part of the in-OS `cc` link search surface. `libtcc1.a` is staged to
`/apps/dev/tcc/` as the TinyCC runtime-helper archive resolved by
`tcc_add_runtime()`.

`dev/building.txt` is user-facing runtime docs; this in-tree guide is the
source-level companion. Keep both in sync so staged `/apps/dev` reality,
phase-gating labels, and quick-start instructions do not drift.

## Writing an app

An in-OS app is the same shape as an in-tree user app:

```c
#include "secureos_api.h"

int main(void) {
    os_console_write("my first app\n");
    return 0;
}
```

- Entry point is `int main(void)` or `int main(int argc, char **argv)`.
  The runtime's `crt0` (`sdk/lib/crt0.c`) marshals args and calls `main`.
- `secureos_api.h` is the public syscall surface
  ([`user/include/secureos_api.h`](../../user/include/secureos_api.h)).
- `return` value from `main` becomes the shell's `$?`.

### Capabilities

Apps run under the SecureOS capability model. A plain console app needs no
extra capabilities — it inherits the console the shell already holds. Apps
that touch the filesystem, network, etc. must request the relevant
capabilities; the launcher/broker grant them per policy (and the user is
prompted where policy requires it). See
[`docs/abi/capabilities.md`](../abi/capabilities.md).

## Filesystem name limits (8.3)

The SecureOS filesystem stores **8.3 names**: ≤8-character name + ≤3-character
extension, case-insensitive. This affects both source and output names:

| Intended       | On disk    | Note                          |
|----------------|------------|-------------------------------|
| `hello.c`      | `HELLO.C`  | fine                          |
| `myutil.c`     | `MYUTIL.C` | fine                          |
| `hello_world.c`| —          | **too long**, shorten to ≤8   |

Compiled binaries use the `.bin` extension. The compiler wraps the produced
ELF in the SecureOS Object Format (SOF) container automatically — you do not
run a separate packing step on-device.

## How a build works *(planned — Phases 3–5)*

```
cc /apps/dev/hello.c -o /apps/hello.bin
  │
  ├─ read source         os_fs_read_file()         [CAP_FS_READ]
  ├─ compile + link       libtcc (in memory)        -> x86_64 ELF
  ├─ wrap                 libsofpack                -> SOF container (.bin)
  └─ write output         os_fs_write_file()        [CAP_FS_WRITE]
```

### Manifest resolution precedence *(planned)*

When `cc` resolves the manifest paired with an output binary, precedence is:
`--manifest <path>` (explicit override) > co-located
`<output>.manifest.json` sidecar > synthesis via `libmanifestgen`.
This contract is pinned by issue [#607](https://github.com/rwrife/SecureOS/issues/607)
and is intentionally deterministic so launcher policy/audit behavior cannot
silently drift.

### Canonical compiler-class app manifest

The canonical manifest contract for the staged in-OS compiler app (`/apps/dev/cc`)
is pinned at [`manifests/apps_dev_cc.json`](../../manifests/apps_dev_cc.json).
It records the expected `owner.kind`, capability request set, and
`runtime.arena_bytes` value for this compiler-class binary.

Highlights of that pin:
- `owner.kind = "internal"` (the `cc` app itself ships in-image).
- Capability request scope is intentionally minimal (`CAP_FS_READ` +
  `CAP_FS_WRITE`) and explicitly excludes `CAP_APP_EXEC` / network capability.
- `runtime.arena_bytes` is pinned as an upper-bound placeholder until
  [#543](https://github.com/rwrife/SecureOS/issues/543) lands a measured value.

Host drift gate:
`build/scripts/test.sh apps_dev_cc_manifest` (wired into
`build/scripts/validate_bundle.sh`) compares the canonical pin above with the
staged app manifest (`user/apps/cc/manifest.json`) and fails on divergence once
`#540` lands the app skeleton.

TinyCC is used (not GCC/Clang) because it compiles, assembles, and links in a
single process — SecureOS cannot spawn separate `as`/`ld` stages. TinyCC is
vendored as a pinned git submodule under
[`vendor/tinycc/`](../../vendor/tinycc).

## Trust model: unsigned local binaries

Official SecureOS binaries are signed with the master ed25519 key. That
private key **never lives on the device**, so anything you compile on-device
is *unsigned*. SecureOS already supports this safely:

- When you run a locally built binary, the loader raises an
  **unsigned-binary prompt** (`AUTH_TYPE_UNSIGNED_BIN`). Choose **Allow** to
  run it once, or **Always** to remember the decision for that binary.
- Denying the prompt blocks execution and records an audit event — the same
  deny path used everywhere else in the OS.

"Official" for an in-OS build therefore means "a well-formed SOF the OS will
run after an explicit user grant," not "master-signed." Promoting a local
binary to a master-signed release remains a host-side, reviewed step. A future
phase may add a per-device "local developer" key to avoid repeated prompts;
see the plan's trust-model section.

## `cc` exit codes v0 (contract pin)

Issue [#589](https://github.com/rwrife/SecureOS/issues/589) pins the numeric
exit surface for the in-OS `cc` driver so QEMU harnesses can assert specific
failure classes (not just "non-zero"). This table is normative for v0.

| Exit code | Class | `cc.compile.*` pairing (issue [#571]) | Harness/assertion owner |
|-----------|-------|-----------------------------------------|-------------------------|
| `0` | Success; output SOF written | `cc.compile.success` | `toolchain_compiles_hello_in_os` ([#567]) and `toolchain_large_output_persisted` ([#569]) assert successful compile/write path |
| `1` | Usage error (bad flag/invocation) | `cc.compile.fail` with `reason=usage_error` | Exit-code conformance harness ([#599]) pins this value |
| `2` | Compile error (syntax/type/undeclared symbol) | `cc.compile.fail` with `reason=compile_error` | `toolchain_compile_error_reported` ([#559]) + conformance harness ([#599]) |
| `3` | Link error (unresolved symbol/missing runtime object) | `cc.compile.fail` with `reason=link_error` | Exit-code conformance harness ([#599]) |
| `4` | I/O error (source read/output write/fs failure) | `cc.compile.fail` with `reason=io_error` | Exit-code conformance harness ([#599]) |
| `5` | Arena exhaustion (`runtime.arena_bytes` cap) | `cc.compile.fail` with `reason=arena_exhausted` | `cc_arena_exhaustion_audit_marker` ([#610]) + conformance harness ([#599]) |
| `64+` | Reserved internal compiler fault range (assert/bug class) | `cc.compile.fail` with `reason=internal_error` (or narrower future subtype) | Reserved/pinned by conformance harness ([#599]); individual values intentionally unassigned in v0 |

Notes:
- This is a contract pin, not the implementation landing. Runtime behavior
  still gates on [#409](https://github.com/rwrife/SecureOS/issues/409) and
  [#410](https://github.com/rwrife/SecureOS/issues/410).
- New reasons may be added later, but existing numeric assignments above are
  stable once this table ships.

## Building the disk image (host side)

[#559]: https://github.com/rwrife/SecureOS/issues/559
[#567]: https://github.com/rwrife/SecureOS/issues/567
[#569]: https://github.com/rwrife/SecureOS/issues/569
[#571]: https://github.com/rwrife/SecureOS/issues/571
[#599]: https://github.com/rwrife/SecureOS/issues/599
[#610]: https://github.com/rwrife/SecureOS/issues/610

The `/apps/dev` content is staged onto `secureos-disk.img` by the normal disk
build:

```bash
./build/scripts/build_disk_image.sh   # inside the Docker toolchain
```

You can verify the staging step in isolation with the host test:

```bash
python3 tests/in_os_toolchain_dev_dir_test.py
```

## Roadmap

For the live acceptance-marker state (including which M7 markers are still
SKIP-pinned vs PASS), see
[`tests/m7_toolchain/markers.json`](../../tests/m7_toolchain/markers.json).
Treat that file as the canonical marker-status source of truth.

| Phase | Deliverable                                              | State    |
|-------|---------------------------------------------------------|----------|
| 1     | `/apps/dev` layout, `hello.c`, docs, TinyCC submodule   | shipped |
| 2     | Userland heap (`os_mem_*`) + freestanding libc          | shipped ([#421](https://github.com/rwrife/SecureOS/issues/421), [#424](https://github.com/rwrife/SecureOS/issues/424), [#495](https://github.com/rwrife/SecureOS/issues/495)) |
| 3     | Freestanding TinyCC (`libtcc`) build                     | in progress ([#408](https://github.com/rwrife/SecureOS/issues/408), [#538](https://github.com/rwrife/SecureOS/issues/538), [#539](https://github.com/rwrife/SecureOS/issues/539)) |
| 4     | `cc` driver + SOF packer (`libsofpack`)                  | scaffolded ([#521](https://github.com/rwrife/SecureOS/issues/521), [#522](https://github.com/rwrife/SecureOS/issues/522), [#533](https://github.com/rwrife/SecureOS/issues/533), [#540](https://github.com/rwrife/SecureOS/issues/540)) |
| 5     | Unsigned-run wiring; `cc` output runs via the launcher   | planned (gated on [#410](https://github.com/rwrife/SecureOS/issues/410)) |
| 6     | `sosh` integration + acceptance test suite               | planned  |

### libc-deps Phase-3 completion gate

`tests/m7_toolchain/markers.json` includes the marker
`toolchain_libc_deps_phase3_complete` to pin the TinyCC libc-deps Phase-3
completion contract.

- While either [#538](https://github.com/rwrife/SecureOS/issues/538) or
  [#539](https://github.com/rwrife/SecureOS/issues/539) is open, the marker is
  intentionally SKIP-pinned (`awaiting_538_539`).
- Once both close, `tools/validate_m7_markers.py` fails CI until the marker is
  flipped off `awaiting_*` (real PASS assertion or explicit retarget), so the
  Phase-3 completion cannot silently remain deferred.

See the plan for the per-phase acceptance tests that gate each step.
