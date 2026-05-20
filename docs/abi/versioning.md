# SecureOS ABI Versioning Policy

This restates `BUILD_ROADMAP.md` §7 in operational terms.

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

Last verified against commit: 9f4f7ccbb19c9ffb28ee4b6de2f3e93c35e65785
