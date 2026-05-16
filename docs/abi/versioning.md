# ABI Versioning Policy

This document mirrors `BUILD_ROADMAP.md` §7 and points at the in-tree
source of truth for the SecureOS ABI version.

## Source of truth

`user/include/secureos_abi.h` defines:

| Macro                   | Current value | Notes                                  |
| ----------------------- | ------------- | -------------------------------------- |
| `OS_ABI_VERSION_MAJOR`  | `0`           | Major; bumped only on incompatible breaks. |
| `OS_ABI_VERSION_MINOR`  | `0`           | Minor; bumped on additive, source-compatible extensions. |
| `OS_ABI_VERSION`        | `0x00000000`  | Packed `(major << 16) \| minor`.       |

No other file in the tree may declare these macros. Anything that needs
to reason about the ABI version at compile time must include
`secureos_abi.h`; anything that needs to query it at runtime must call
`os_get_abi_version()` (declared in `secureos_api.h`, implemented in
`user/runtime/secureos_api_stubs.c`).

## Field layout

`OS_ABI_VERSION` is a 32-bit unsigned integer:

```
bits 31..16 : major
bits 15..0  : minor
```

Decode with `(v >> 16) & 0xFFFF` and `v & 0xFFFF`.

## Lifecycle policy (BUILD_ROADMAP §7)

- **0.x** — rapid iteration; ABI may change between commits with a
  noted entry in `MASTER_LOG.md`. No external SDK consumers yet.
- **1.0** — frozen at SDK beta announcement (see #136). After 1.0,
  break changes require a major bump and a compatibility shim that
  remains supported for at least one major version.
- Shims live alongside the user runtime under `user/runtime/` and are
  selected by inspecting `os_get_abi_version()` at startup.

## What is covered

The version covers, collectively:

- the user-facing syscall surface in `user/include/secureos_api.h`,
- the capability handle representation and revocation semantics,
- the IPC wire format and error model,
- the module manifest schema and compatibility policy.

The manifest *schema version* is a separate field (see #93) layered on
top of this overall ABI version.

## Test coverage

`tests/abi_version_test.c` (driven by
`build/scripts/test_abi_version.sh`) asserts:

1. `OS_ABI_VERSION_MAJOR == 0` and `OS_ABI_VERSION_MINOR == 0`,
2. the packed layout matches `(major << 16) | minor`,
3. `os_get_abi_version()` returns the same value the header advertises
   (catches stale runtime stubs after a bump).
