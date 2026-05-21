# SecureOS ABI Versioning Policy

This restates `BUILD_ROADMAP.md` §7 in operational terms and points at
the in-tree source of truth for the SecureOS ABI version.

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

### Field layout

`OS_ABI_VERSION` is a 32-bit unsigned integer:

```
bits 31..16 : major
bits 15..0  : minor
```

Decode with `(v >> 16) & 0xFFFF` and `v & 0xFFFF`.

## States

- `OS_ABI_VERSION = 0` — **current.** Rapid iteration. Surface listed in
  this directory is the working set; signatures may change with a single
  PR that updates every caller in-tree. Out-of-tree consumers do not
  exist yet.
- `OS_ABI_VERSION = 1` — frozen at SDK beta announcement. After freeze:
  - No removal or signature change of any documented call/field without
    a major bump.
  - New additions are allowed and remain at version `1`.
  - Compatibility shims for the previous major are maintained for **at
    least one major version** (i.e. when we move to `2`, `1` callers
    still work for that release line).
- `OS_ABI_VERSION = N (N ≥ 2)` — same rules as `1`, with the rolling
  one-major-version shim window.

## What counts as ABI

- Anything declared in `user/include/secureos_api.h`.
- Anything in `kernel/cap/capability.h` that is exposed by name to user
  space or to validators (capability IDs, result codes, audit event
  layout, checkpoint layout).
- The launcher manifest schema in [manifest.md](manifest.md).
- The validator JSON report schema (`build/scripts/validate_bundle.sh`)
  and the `TEST:PASS:` / `TEST:FAIL:` marker contract — validators and
  CI depend on these.

## Process for an ABI change

1. Open an issue describing the change and the user impact.
2. Update the relevant doc under `docs/abi/`.
3. Bump the `Last verified against commit` line in the same PR as the
   code change.
4. After SDK beta freeze, add a shim or a `manifest_version` bump if the
   change is not strictly additive.

## What is *not* ABI yet

- Internal kernel APIs (`kernel/cap/cap_table.h` internals beyond the
  documented enum, scheduler interfaces, HAL details).
- The on-disk app binary format (`docs/plans/2026-03-16-secureos-file-format.md`)
  — still in design.
- The native bridge layout in `user/runtime/secureos_api_stubs.c` — this
  is an implementation detail of the M1 host bridge and will be
  replaced by real syscalls.

## Test coverage

`tests/abi_version_test.c` (driven by
`build/scripts/test_abi_version.sh`) asserts:

1. `OS_ABI_VERSION_MAJOR == 0` and `OS_ABI_VERSION_MINOR == 0`,
2. the packed layout matches `(major << 16) | minor`,
3. `os_get_abi_version()` returns the same value the header advertises
   (catches stale runtime stubs after a bump).

Last verified against commit: 9f4f7ccbb19c9ffb28ee4b6de2f3e93c35e65785
