# SecureOS ABI Versioning

> Source of truth for the `OS_ABI_VERSION` constant lives in
> [`user/include/secureos_abi.h`](../../user/include/secureos_abi.h).
> Any change to that header must be reflected here and exercised by
> [`tests/abi_version_test.c`](../../tests/abi_version_test.c).

This document is the initial stub created for issue #150 to anchor the
ABI version constant prescribed by [BUILD_ROADMAP.md §7](../../BUILD_ROADMAP.md#7-abi-and-interface-freeze-plan).
A fuller reference (manifest schema, capability handle layout, full
syscall surface) is tracked by issue #93 and may supersede or extend
this file.

## Constants

`OS_ABI_VERSION` is a packed unsigned 32-bit value:

| Bits   | Field | Current value |
| ------ | ----- | ------------- |
| 16..31 | major | `0`           |
| 0..15  | minor | `0`           |

```c
#define OS_ABI_VERSION_MAJOR 0u
#define OS_ABI_VERSION_MINOR 0u
#define OS_ABI_VERSION (((unsigned int)OS_ABI_VERSION_MAJOR << 16) | \
                        ((unsigned int)OS_ABI_VERSION_MINOR & 0xFFFFu))
```

## Accessor

```c
unsigned int os_get_abi_version(void);
```

- Information-only. No capability required.
- Returns the packed `OS_ABI_VERSION` that the runtime stubs (or, in a
  future build, the kernel syscall surface) were built against.
- Callers should treat a mismatch versus their compile-time
  `OS_ABI_VERSION` as a build-environment error: the runtime stubs are
  out of sync with the headers used to compile the app.

## Policy (per BUILD_ROADMAP §7)

- Start at `OS_ABI_VERSION_MAJOR = 0` during rapid iteration.
- Freeze to `1` when the public SDK beta is announced
  (see issue #136, "Plan: M6 public SDK").
- Maintain compatibility shims for **at least one** previous major
  version after a bump.

### Bumping rules (pre-SDK-beta, major = 0)

- Additive, backward-compatible changes (new syscall stub, new field at
  end of a struct): bump **minor** in `secureos_abi.h`.
- Breaking changes are allowed without a major bump while
  `OS_ABI_VERSION_MAJOR == 0`, but each one MUST update this document
  and the test in `tests/abi_version_test.c`.

### Bumping rules (post-SDK-beta, major ≥ 1)

- Additive: bump minor.
- Breaking: bump major, ship shims for previous major, document the
  removed/changed surface in a dedicated migration note under
  `docs/abi/`.

## Test coverage

- `tests/abi_version_test.c` (registered as `abi_version` in
  `build/scripts/test.sh` and `build/scripts/validate_bundle.sh`)
  asserts:
  1. The packed `OS_ABI_VERSION` decomposes back into the major/minor
     constants (bit-layout sanity).
  2. `OS_ABI_VERSION_MAJOR == 0` (pinned until SDK beta).
  3. `os_get_abi_version()` returns the same value the header defines
     (stale-stub guard).

## Related

- BUILD_ROADMAP.md §7 — policy source
- Issue #150 — this anchor
- Issue #93 — full ABI reference (manifest schema, capability handles,
  syscall surface)
- Issue #136 — M6 public SDK plan (consumer of this version)
