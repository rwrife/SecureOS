# SecureOS Public SDK (scaffold)

Status: **scaffolding in progress** — slices 1 and 2 of plan
[`plans/2026-05-15-public-sdk-external-app-template.md`](../plans/2026-05-15-public-sdk-external-app-template.md)
(M6 in `BUILD_ROADMAP.md` §5.6, tracked by execute issues
[M6-SDK-001](https://github.com/rwrife/SecureOS/issues/369) and
[M6-SDK-002](https://github.com/rwrife/SecureOS/issues/388)).

This directory is the **only** surface external apps may depend on. Anything
under `kernel/` or in-tree-only paths under `user/` is **not** part of the
SDK contract.

## What this slice ships

- `VERSION` — plain-text ABI version pin. Its content MUST equal the
  packed `OS_ABI_VERSION` value advertised by
  `user/include/secureos_abi.h`, expressed as `MAJOR.MINOR.PATCH`
  (PATCH is reserved and currently always `0`).
- `include/os/abi.h` — public, freestanding-safe re-export of
  `OS_ABI_VERSION_{MAJOR,MINOR,PATCH}` from the in-tree source of truth
  at `user/include/secureos_abi.h`. The SDK does **not** mint a second
  ABI version constant; it forwards the one the kernel ships.
- `lib/crt0.c` — SDK-owned C runtime entry shim (slice `M6-SDK-002`).
  Provides the `_start` symbol external apps link as their loader
  entry point; marshals `os_get_args()` into the standard
  `int main(int argc, char **argv)` signature.
- `lib/libos/version.c` — SDK-owned ABI re-export anchor (slice
  `M6-SDK-002`). The actual `os_get_abi_version()` definition is
  contributed to `libos.a` by `user/runtime/secureos_api_stubs.c`;
  this file exists to keep the SDK header (`os/abi.h`) and the
  in-tree source of truth (`secureos_abi.h`) in lockstep at
  build time.
- `lib/` — archive target. `artifacts/sdk/libos.a` is produced by
  `build/scripts/build_sdk_libos.sh` (composing the slice-2 sources
  with the existing `user/runtime/secureos_api_stubs.c` — strict
  re-export, no new ABI opcodes).
- `tools/` — placeholder for `os-cc`, `os-pack`, `os-run`; arrives in
  slice `M6-SDK-003`.

## What this slice does NOT ship

Deferred to the explicit follow-up slices enumerated in the plan:

- `os-cc` / `os-pack` / `os-run` wrappers (slice `M6-SDK-003`).
- Manifest schema additions (`abi.version`,
  `capabilities.required/optional`) — slice `M6-SDK-003`.
- `samples/hello-from-sdk/` external app — slice `M6-SDK-004`.
- A real kernel-side user-exit syscall to back `crt0.c`'s `_os_exit()`
  helper — currently a `hlt`-loop placeholder (slice `M6-SDK-003`).

## Determinism / containment rules (enforced by CI)

- No source file under `sdk/` may `#include` anything from `kernel/`.
  The `sdk/` tree is downstream of the kernel ABI surface, never upstream
  of it. Slice 1 only ships headers + this README, but the
  `validate_sdk_no_kernel_includes` check is wired so the rule is real
  the moment any future slice tries to violate it.
- `sdk/VERSION` is the single externally visible version string; CI
  (`tests/sdk_abi_pin_test.c`) re-checks it against `secureos_abi.h` on
  every build so the SDK cannot silently drift from the kernel ABI.

## Layout

```
sdk/
  VERSION                 # MAJOR.MINOR.PATCH — matches OS_ABI_VERSION
  README.md               # this file
  include/os/
    abi.h                 # re-exports OS_ABI_VERSION_{MAJOR,MINOR,PATCH}
  lib/
    crt0.c                # `_start` entry shim (slice M6-SDK-002)
    libos/
      version.c           # SDK ABI re-export anchor (slice M6-SDK-002)
  tools/                  # slice M6-SDK-003 — os-cc / os-pack / os-run
```
