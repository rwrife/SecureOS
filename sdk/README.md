# SecureOS Public SDK (scaffold)

Status: **scaffolding only** — slice 1 of plan
[`plans/2026-05-15-public-sdk-external-app-template.md`](../plans/2026-05-15-public-sdk-external-app-template.md)
(M6 in `BUILD_ROADMAP.md` §5.6, tracked by execute issue
[M6-SDK-001](https://github.com/rwrife/SecureOS/issues/369)).

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
- `lib/` — placeholder for the userland library (`libos.a`); arrives in
  slice 2 (`M6-SDK-002`).
- `tools/` — placeholder for `os-cc`, `os-pack`, `os-run`; arrives in
  slice 2.

## What this slice does NOT ship

Deferred to the explicit follow-up slices enumerated in the plan:

- `os-cc` / `os-pack` / `os-run` wrappers (slice `M6-SDK-002`).
- Manifest schema additions (`abi.version`,
  `capabilities.required/optional`) — slice `M6-SDK-003`.
- `samples/hello-from-sdk/` external app — slice `M6-SDK-004`.
- `libos.a` userland library + `crt0.c` — slice `M6-SDK-002`.

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
  lib/                    # slice M6-SDK-002 — libos.a + crt0.c
  tools/                  # slice M6-SDK-002 — os-cc / os-pack / os-run
```
