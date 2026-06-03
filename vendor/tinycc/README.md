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
