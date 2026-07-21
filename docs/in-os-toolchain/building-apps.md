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

## Where the tools live

Everything for on-device development is under **`/apps/dev`** on the disk:

| Path                      | Purpose                                              |
|---------------------------|------------------------------------------------------|
| `/apps/dev/hello.c`       | Validation sample — compile it to test the toolchain |
| `/apps/dev/building.txt`  | The on-device quick-start (a short version of this)  |
| `/apps/dev/cc` *(planned)*       | The in-OS C compiler (TinyCC-based)           |
| `/apps/dev/include/` *(planned)* | Public headers (`secureos_api.h`, `os/*.h`)   |

The repo source that gets staged to `/apps/dev` lives in the top-level
[`dev/`](../../dev) directory (analogous to how `scripts/` is staged to
`/scripts`). It is wired into the image by
[`build/scripts/build_disk_image.sh`](../../build/scripts/build_disk_image.sh).

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

## Building the disk image (host side)

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

| Phase | Deliverable                                              | State    |
|-------|---------------------------------------------------------|----------|
| 1     | `/apps/dev` layout, `hello.c`, docs, TinyCC submodule   | **this** |
| 2     | Userland heap (`os_mem_*`) + freestanding libc          | planned  |
| 3     | Freestanding TinyCC (`libtcc`) build                     | planned  |
| 4     | `cc` driver + SOF packer (`libsofpack`)                  | planned  |
| 5     | Unsigned-run wiring; `cc` output runs via the launcher   | planned  |
| 6     | `sosh` integration + acceptance test suite               | planned  |

See the plan for the per-phase acceptance tests that gate each step.
