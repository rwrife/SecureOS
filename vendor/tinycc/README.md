# TinyCC for SecureOS

## Overview

[TinyCC](https://repo.or.cz/tinycc.git) (TCC) is a small, fast C compiler
that performs **compilation, assembly, and linking in a single process** with
no external `as`/`ld`. That property is what makes it viable as an *in-OS*
compiler for SecureOS: the OS has no `fork`/`exec` of separate toolchain
stages, so a self-contained compiler is the only realistic option.

TCC is the compiler backing the in-OS toolchain milestone — the ability to
write a `.c` file and produce a runnable SecureOS binary (SOF) **from inside
a running SecureOS instance**. See
[`plans/2026-05-28-in-os-toolchain-self-hosting.md`](../../plans/2026-05-28-in-os-toolchain-self-hosting.md).

## Submodule

TinyCC is included as a git submodule pinned to the commit recorded in
[`VERSION`](VERSION), at `vendor/tinycc/tinycc/`.

After cloning the repo, initialize the submodule:

```bash
git submodule update --init vendor/tinycc/tinycc
```

CI initializes all submodules via an explicit `git submodule update
--init --recursive` step in each workflow (see
`.github/workflows/*.yml`, #520). `actions/checkout@v4`'s
`submodules: recursive` is not used because it does a shallow
submodule fetch that fails against the bearssl.org git server
(server only advertises tip, not pinned commits); a full clone
works for both vendor trees.
Local clones need `git submodule update --init --recursive` before
running `build/scripts/test.sh tinycc_*`.

## Status

> **First-compile slice (issue #766): vendored + compiling.** The
> submodule is pinned, and the pinned freestanding source set now has a
> measured compile: the `tinycc_freestanding_compile` host gate builds
> every `Makefile.secureos` core TU with the SecureOS userland flags and
> produces `artifacts/user/libs/libtcc1.a` (the runtime-helper archive)
> via `build/scripts/build_tinycc_libtcc1.sh`. TinyCC is **not yet an
> executable in-OS compiler** — the remaining #766 work is the libtcc
> link step (fdopen/strerror/strtod/ldexpl call-site shims are
> declaration-only in `vendor/tinycc/include/tcc-compat.h`), loader/SOF
> integration, the memory budget, and the in-guest compile proof.

## Freestanding include surface (`vendor/tinycc/include/`)

TinyCC `#include`s system headers that a freestanding target has no
provider for. This directory supplies the SecureOS-side answers (the
submodule stays verbatim; porting note 1 calls them "config + include"
plumbing):

| File                | Purpose                                                      |
|---------------------|--------------------------------------------------------------|
| `config.h`          | Redirect to `../config-secureos.h` (the pinned config)       |
| `unistd.h`          | Forwards to clib `posix_fd.h` (fd surface, #538)             |
| `time.h`            | Forwards to clib `runtime_compat.h` (deterministic time, #539) |
| `fcntl.h`           | open() flag constants matching posix_fd.h values             |
| `math.h`            | Declaration-only `ldexpl` (single math call in TCC_ALL_SRCS) |
| `sys/time.h`        | Empty TU (nothing from it used by in-scope sources)          |
| `tcc-compat.h`      | `-include`d call-site declarations (fdopen/strerror/strtod) + clib split-header pulls (qsort) |

These are TinyCC-build glue, NOT clib public ABI — `docs/abi/clib-symbols.md`
and the `clib_symbol_drift` pin are unaffected.

## Build

The freestanding compile is automated by two scripts (both run inside the
pinned Docker toolchain, same posture as BearSSL's `build_bearssl.sh`):

```bash
# runtime-helper archive only (also `scripts/build.sh tinycc`)
bash build/scripts/build_tinycc_libtcc1.sh

# gate: compile every core TU + build the archive + member parity
bash build/scripts/test.sh tinycc_freestanding_compile
```

## Files in this directory

| File                | Purpose                                                       |
|---------------------|---------------------------------------------------------------|
| `tinycc/`           | Git submodule — pinned TinyCC sources                          |
| `VERSION`           | Upstream pin (commit, branch, license)                        |
| `Makefile.secureos` | Freestanding build file list + porting notes (scaffold)       |
| `LICENSE`           | License pointer + the LGPL obligation note for SecureOS       |

## Drift gate (`tinycc_vendor_gate`)

While the freestanding port (#408) is in flight, the vendor surface itself is
pinned by a deterministic drift gate (mirrors BearSSL's `bearssl_compile`
from #117):

```bash
bash build/scripts/test.sh tinycc_vendor_gate
```

The gate asserts:

- `Makefile.secureos` enumerates the in-scope `.c` list explicitly (no
  globbing) and meets the documented 9-file minimum (libtcc core +
  x86\_64 backend + shared i386 assembler).
- Deliberately-excluded surfaces (`tccrun.c` JIT, `tcc.c` CLI main,
  `tccpe.c` / `tccmacho.c` / `tcccoff.c` non-ELF formats, `tcctools.c`,
  and every non-x86\_64 backend) are **absent** from the list.
- `VERSION` carries a well-formed 40-hex `Commit:` SHA and — when the
  submodule has been initialized — the pin SHA matches the live
  submodule HEAD and every listed source file exists.

The gate is wired into `build/scripts/validate_bundle.sh`'s `TEST_TARGETS`,
so any silent surface change (vendor scope creep, pin/submodule
mismatch, missing source) flips the bundle to FAIL.

## Arena measurement pin (`tinycc_arena_drift`)

Issue [#543](https://github.com/rwrife/SecureOS/issues/543) adds
`vendor/tinycc/arena-measurements.json` as the source of truth for TinyCC
compile-time arena sizing inputs consumed by the future `cc` manifest work
(#409). The companion host gate is:

```bash
bash build/scripts/test.sh tinycc_arena_drift
```

Current state is intentionally SKIP-pinned (`awaiting_408_phase3`) while
`Makefile.secureos` remains scaffold-only and #408 Phase 3 has not landed.
Even in SKIP mode, the gate validates fixture TU hashes and the pinned
recommendation document so drift stays explicit. After each TinyCC vendor bump
(or once Phase 3 lands and live measurements are available), regenerate/update
`vendor/tinycc/arena-measurements.json` via:

```bash
python3 tools/measure_tinycc_arena.py --root . \
  --output vendor/tinycc/arena-measurements.json
```

## License

TinyCC is licensed under **LGPL-2.1** (see `tinycc/COPYING`). This is a more
restrictive license than SecureOS's other vendored dependency (BearSSL, MIT),
and statically linking it into a shipped OS image carries relink /
source-availability obligations. See [`LICENSE`](LICENSE) for the details and
the decision record. The `tinycc/RELICENSING` file records the subset of
contributors who have consented to MIT relicensing of their contributions.

### Shipping-side obligations

The distribution-time companion to this in-tree decision lives at
[`docs/legal/lgpl-compliance.md`](../../docs/legal/lgpl-compliance.md):
it defines the LGPL-2.1 compliance bundle (TinyCC source tarball, libtcc
object, SecureOS-side relink objects, license texts) that must accompany
every released image. Produced by
`build/scripts/build_release_compliance_bundle.sh`; gated by the
`release_compliance_bundle` host test (SKIP-pinned until #408 Phase 3).
