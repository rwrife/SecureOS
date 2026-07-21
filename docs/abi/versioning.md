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

## Manifest schema evolution policy

For manifest changes, classify the change before deciding whether to bump
`OS_ABI_VERSION`:

- **Additive** (no bump):
  - new optional field with a safe default that preserves prior behavior when
    omitted,
  - new enum value behind an existing discriminator where old values keep the
    same meaning,
  - new numeric policy field with explicit clamp/cap and unchanged omit-path
    behavior.
- **Breaking** (minor bump):
  - renaming/removing an existing field,
  - changing a field default in a way that changes launcher/runtime behavior,
  - narrowing an enum or reinterpreting an existing value.
- **Incompatible** (major bump):
  - wire-format break,
  - syscall opcode reuse/repurpose,
  - any change that prevents an existing conforming artifact from being safely
    interpreted.

### Worked examples

| Change | Classification | Required bump | Reference |
| --- | --- | --- | --- |
| Add `owner.kind="local"` while preserving `"internal"` default behavior | Additive | None | [#522](https://github.com/rwrife/SecureOS/issues/522) |
| Add optional `runtime.arena_bytes` with bounded range and unchanged omit-path semantics | Additive | None | [#424](https://github.com/rwrife/SecureOS/issues/424) |
| Add optional `capabilities.ownership_role` enum with `"none"` default preserving prior behavior | Additive | None | [#368](https://github.com/rwrife/SecureOS/issues/368) |
| Synthetic example: rename `launcher.auto_grant_at_launch` or change its default grant semantics | Breaking | Minor (`OS_ABI_VERSION_MINOR`++) | Policy example |

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

## Stamp discipline for new ABI docs (#470)

Every file under `docs/abi/` that participates in the ABI freshness
contract MUST carry a `Last verified against commit: <sha>` line. The
stamp is checked by `tools/validate_abi_stamps.py` (wrapper:
`build/scripts/validate_abi_stamps.sh`) under the
`TEST:PASS:validate_abi_stamps` bundle marker; an unstamped file is
currently treated as `ABI_STAMP:SKIP:<file>:no_stamp_line` (exit 0) as a
bootstrap concession for legacy docs.

**Strict-no-skip mode** (issue #470) promotes that SKIP to
`ABI_STAMP:FAIL:<file>:no_stamp_line` (exit 1). It can be enabled two
ways:

- CLI: `tools/validate_abi_stamps.py --strict-no-skip`
- Wrapper: `STRICT_STAMPS=1 build/scripts/validate_abi_stamps.sh`

A negative canary (`tests/harness/abi_stamps_strict_no_skip_test.sh`,
wired as `TEST:PASS:abi_stamps_strict_no_skip` in `build/scripts/test.sh`
and `validate_bundle.sh`) keeps both arms honest:

- default mode still emits `ABI_STAMP:SKIP:<file>:no_stamp_line` and
  exit 0 for an unstamped doc, and
- strict mode emits `ABI_STAMP:FAIL:<file>:no_stamp_line` and exit 1
  for the same doc, while files passed via `--exempt <name>` are
  dropped from iteration entirely (matching the existing escape hatch
  used by `capability-registry.md`).

**Rule for new ABI surface PRs:** any newly-added `docs/abi/*.md` MUST
include a `Last verified against commit:` line in the same change.
Genuinely-non-freshness docs (e.g. pure index pages) MUST be added to
the wrapper's `--exempt` list in the same PR with a one-line rationale.
The wrapper's default will flip to strict (`STRICT_STAMPS=1` becomes the
implicit default) once the four pre-existing `no_stamp_line` SKIP files
(`docs/abi/manifest.md` #463, `docs/abi/clib-symbols.md` #468,
`docs/abi/capability-deny-contract.md` PR #477,
`docs/abi/sosh-capability-contract.md` PR #478) all carry stamps.

Last verified against commit: 99ae65d37daaa9d5dbffe2889ba64f2005946b94
