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

## Status

> **Phase 1 (this slice): vendored only.** The submodule is pinned and the
> wrapper scaffolding is in place. TinyCC is **not yet built or ported** to
> the freestanding SecureOS target — that is the work tracked by Phases 2–5
> of the plan (userland heap, freestanding libc, freestanding TCC build, the
> `cc` driver app, and SOF packaging). `Makefile.secureos` documents the
> intended build but does not yet produce a working compiler.

## Build (target shape — not yet functional)

The eventual freestanding build compiles TCC's core (`libtcc`) against the
SecureOS userland C library and links it into the `cc` app under
`user/apps/cc/`. See [`Makefile.secureos`](Makefile.secureos) for the file
list and the porting notes.

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

## License

TinyCC is licensed under **LGPL-2.1** (see `tinycc/COPYING`). This is a more
restrictive license than SecureOS's other vendored dependency (BearSSL, MIT),
and statically linking it into a shipped OS image carries relink /
source-availability obligations. See [`LICENSE`](LICENSE) for the details and
the decision record. The `tinycc/RELICENSING` file records the subset of
contributors who have consented to MIT relicensing of their contributions.
