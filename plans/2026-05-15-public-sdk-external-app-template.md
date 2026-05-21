# 2026-05-15 M6 Public SDK + External App Template Slice

## Goal
Deliver the M6 vertical slice from BUILD_ROADMAP §5.6: a public SDK
(headers + userland library + tool wrappers) and a third-party-shaped
sample app that builds **outside the SecureOS source tree** and runs in
QEMU under the launcher/broker, with its declared manifest capabilities
enforced end-to-end.

Restating the roadmap deliverables:

> Deliver:
> - headers + userland library
> - tool wrappers (`os-cc`, `os-pack`, `os-run`)
> - manifest schema and ABI versioning guide
>
> Validate:
> - third-party sample app builds and runs in QEMU
> - manifest capability declarations enforced by launcher/broker

This plan is a *plan only*. The acceptance tests below are the contract a
follow-up "Execute plan: M6 public SDK + external app template" issue will
implement; no code lands as part of #136.

## Scope
- Carve a `sdk/` top-level directory whose contents are the **only** surface
  external apps may depend on. Anything under `kernel/` or `tools/` stays
  internal.
- Stand up three thin tool wrappers (`os-cc`, `os-pack`, `os-run`) so an
  external author never invokes the in-tree `tools/sof_wrap`, `i686-elf-gcc`,
  or QEMU directly.
- Pin the SDK to a single ABI version constant (`OS_ABI_VERSION`) and refuse
  to load apps whose manifest declares a different major.
- Ship one minimal external sample (`samples/hello-from-sdk/`) that lives
  under a path the build treats as out-of-tree, so the wrappers' include /
  link / sign / run paths are exercised on a "fresh" tree.
- Add manifest schema fields for *declared capabilities* and ABI version pin,
  and wire the launcher/broker to enforce both at load.

Out of scope:

- C++ / Rust / dynamic linking. The SDK is C-only and statically links the
  userland library, mirroring the existing `kernel/user/` ABI.
- Package distribution (no `os-pkg install`, no registry). `os-pack` only
  produces a signed SOF; how it gets onto the disk image is the same path
  M2–M5 already use.
- Cross-compilation from non-Linux hosts. The toolchain container from
  §3 / §6.1 stays the supported build environment.
- IDE integration, language server, debugger ABI.
- Reparenting / capability transfer from external apps (M5's #118 plan
  already declares this out of scope).

## Dependencies
- **#118 — M5 ownership graph + cascading deletion plan.** External apps
  are first-class owners; the SDK contract must promise that deleting an
  external app cascades exactly like an in-tree app. This plan assumes the
  edge shape and `delete_owner` semantics from #118 are authoritative.
- **#93 / PR #97 — ABI reference docs.** Hard precondition: the SDK headers
  *are* the ABI surface that #93 documents. The execute issue must not ship
  a header that #93 has not described, and any new entry point added here
  must update the ABI reference in the same PR.
- **#117 / PR #122 — BearSSL vendor discipline.** Sets the precedent for
  `vendor/`-style external-source layout, version pinning, and validator
  shape that this plan mirrors for `sdk/`.
- **#136 (this issue)** is planning-only; the execute follow-up is gated on
  #118 + #97 + #122 landing so the SDK headers, ownership semantics, and
  vendor discipline they each define are real before they get re-exported.

## Layout

```
sdk/
  VERSION                      # OS_ABI_VERSION = "1.0.0"
  README.md                    # what the SDK is, what it isn't
  include/os/
    abi.h                      # OS_ABI_VERSION_{MAJOR,MINOR,PATCH}
    syscall.h                  # public syscall numbers + arg structs
    cap.h                      # capability handle + cap_t opaque
    console.h                  # console_write/read (CAP_CONSOLE_*)
    fs.h                       # fs_open/read/write/close (CAP_FS_*)
    net.h                      # net_open/send/recv (CAP_NET_*)
    manifest.h                 # in-source manifest annotations (optional)
  lib/
    libos.a                    # produced from sdk/lib/src/*.c
    src/
      crt0.c                   # entry, arg/env unpack, exit syscall
      console.c                # thin shims over syscall.h
      fs.c
      net.c
  tools/
    os-cc                      # wraps i686-elf-gcc + sdk/include + libos.a
    os-pack                    # wraps tools/sof_wrap + manifest validator
    os-run                     # boots QEMU image with --launch=<sof> arg
samples/
  hello-from-sdk/
    Makefile                   # uses os-cc / os-pack only; no in-tree paths
    manifest.json              # declares CAP_CONSOLE_WRITE only
    main.c                     # one printf-equivalent via console.h
```

The `sdk/` directory is the **only** thing the wrappers expose to external
authors. `os-cc` rejects `-I` paths that escape `sdk/include/`, and refuses
to link any `.a` other than `sdk/lib/libos.a` unless `--allow-unstable`
(reserved for in-tree integration tests) is passed.

## Tool Wrapper Contracts

### `os-cc` — compile against SDK headers
- Inputs: `.c` sources + optional `-I<dir>` (whitelisted to `sdk/include/`
  and the source's own dir) + optional `-o <out>`.
- Behavior: invokes the pinned cross-compiler (same one `kernel/` uses)
  with `-ffreestanding -nostdinc -isystem sdk/include -L sdk/lib -los`
  and `-DOS_ABI_VERSION=<sdk/VERSION>`.
- Determinism: refuses to run if `sdk/VERSION` mismatches the cross-compiler
  toolchain version recorded in `sdk/lib/libos.a`'s build-id note.
- Exit codes: 0 on success, `78` on toolchain misconfiguration (matches
  the harness convention from #91 / PR #96 so CI can distinguish infra
  breakage from a real compile error).

### `os-pack` — produce signed SOF + manifest
- Inputs: `<elf-from-os-cc>` + `manifest.json`.
- Behavior:
  1. Validate `manifest.json` against the schema below; reject unknown
     capabilities and unknown ABI version majors.
  2. Append the canonicalised manifest as the SOF metadata segment.
  3. Invoke `tools/sof_wrap` to sign the combined payload with the
     development signing key (or a user-supplied key path).
- Output: `<name>.sof` ready for `os-run` or staging onto a disk image.
- Determinism: byte-identical output for byte-identical inputs (no
  timestamps, no random padding); validated by re-pack-and-diff in CI.

### `os-run` — launch via launcher with declared caps
- Inputs: `<name>.sof` + optional `--cap CAP_FOO` overrides.
- Behavior: boots the deterministic QEMU image with a launch directive
  that hands `<name>.sof` to the launcher. The launcher reads the
  manifest, asks the broker for each declared cap, and starts the
  process — exactly the path used for in-tree apps.
- Without `--cap` overrides, the broker grants caps the manifest
  declares **and the launcher policy currently allows**; declared caps
  the policy denies cause the launch to fail with the same audit event
  as any other deny (#84 / PR #98).

## Manifest Schema (additions)

Today's manifest carries only `name` and `entry`. M6 adds:

```jsonc
{
  "name": "hello-from-sdk",
  "entry": "main",
  "abi": { "version": "1.0.0" },          // NEW: pin major
  "capabilities": {                       // NEW: declared caps
    "required": ["CAP_CONSOLE_WRITE"],
    "optional": []
  },
  "owner": { "kind": "external" }         // NEW: aligns with #118 owner_id
}
```

Validation rules (enforced by `os-pack` *and* re-checked by the launcher):

- `abi.version.major` must equal `OS_ABI_VERSION_MAJOR`. Mismatch ⇒ reject
  at pack time, and reject again at launch time if a hand-edited SOF
  bypasses the wrapper.
- Every entry in `required` must be a known cap symbol from `cap.h`.
  Unknown ⇒ reject at pack time.
- `optional` caps that the broker denies do not abort launch; the app
  starts with those handles unbound and `cap_is_valid()` returns false
  for them.
- `owner.kind` informs ownership-edge labelling per #118; `external`
  owners get the same cascade semantics as in-tree owners.

A canonical manifest schema doc lives at `sdk/include/os/manifest.md` and
is the single source of truth — the ABI reference (#93) links it rather
than re-stating it.

## ABI Versioning Guide
- Single `OS_ABI_VERSION` (semver) lives at `sdk/VERSION` and is mirrored
  into `sdk/include/os/abi.h` at build time.
- Major bump ⇒ binary-incompatible: existing SOFs are rejected by the
  launcher. Required when any syscall number, struct field, or capability
  enum value changes meaning.
- Minor bump ⇒ additive only: new syscalls / new caps / new manifest
  fields with safe defaults. Existing SOFs continue to load.
- Patch bump ⇒ no ABI change; documentation / `libos.a` internal fixes.
- Every SDK-affecting PR must edit `sdk/VERSION` *and* update the ABI
  reference (#93) in the same change. CI gates on this in the execute
  issue (`build/scripts/test.sh sdk_version_consistency`).

## Sample External App: `hello-from-sdk`
- Lives under `samples/hello-from-sdk/`, but the execute issue's CI also
  copies it to a scratch dir outside the repo and builds it from there
  to prove the wrappers do not silently rely on in-tree paths.
- `main.c`: calls `console_write("hello from sdk\n")` and exits 0.
- `manifest.json`: declares `CAP_CONSOLE_WRITE` as required; nothing
  else.
- `Makefile`: three rules — `os-cc` to ELF, `os-pack` to SOF,
  `os-run` to launch.

## Acceptance Tests

All tests follow the `TEST:PASS:` / structured JSON-report contract from
#110 (PR #112) and live under `tests/m6_sdk/`. Each runs in QEMU off the
deterministic disk image except `sdk_external_build_isolation` which runs
on the host.

### `sdk_external_build_isolation`
- Copy `samples/hello-from-sdk/` to a scratch directory outside the repo.
- Invoke `os-cc` + `os-pack` from that directory with **no** `-I` or `-L`
  pointing back into the source tree.
- Assert: build succeeds; produced SOF is byte-identical to a second
  build from the same inputs (determinism check).
- Assert: same build with `-I kernel/include` injected fails with a
  whitelist-violation error from `os-cc`.

### `sdk_external_app_runs_in_qemu`
- Stage the `hello-from-sdk.sof` produced above onto the deterministic
  disk image.
- Boot, let the launcher load it.
- Assert: QEMU serial log contains `hello from sdk`.
- Assert: launcher audit (#84) records exactly one `cap.granted` event
  for `CAP_CONSOLE_WRITE` to this app, and no other grants.

### `sdk_manifest_cap_enforced`
- Modify the sample to additionally call `fs_open()` (which it has not
  declared in its manifest).
- Repack and run.
- Assert: `fs_open()` returns the deny error and the audit ring has a
  `cap.denied` record with reason `not_declared` for `CAP_FS_READ`.
- Assert: the app keeps running after the deny (no kernel panic).

### `sdk_manifest_abi_mismatch_rejected`
- Take the working `hello-from-sdk.sof` and hand-edit its manifest
  `abi.version.major` to `OS_ABI_VERSION_MAJOR + 1`.
- Re-sign with `tools/sof_wrap` to keep signature valid (per #138's
  re-sign discipline this is the only legitimate way to test this).
- Attempt to launch.
- Assert: launcher refuses with `abi_mismatch`, the app never executes,
  and an audit `launch.denied` record is emitted.

### `sdk_owner_cascade_external`
- Boot, run `hello-from-sdk`, then `delete_owner(<external_app_id>)`
  via the broker (per #118's contract).
- Assert: the ownership cascade revokes the `CAP_CONSOLE_WRITE` edge
  the launcher delegated; subsequent re-launch attempts go through a
  fresh grant flow.
- Assert: a `cap.cascade.done` summary is emitted with `n_children == 1`.

Each test exits with `TEST:PASS:m6_sdk/<name>` on success, and the JSON
validator report (#110) gains an `m6_sdk` section.

## Validation Strategy
- `build/scripts/test.sh m6_sdk` runs the five tests above; CI gates on
  the JSON report.
- `build/scripts/validate_bundle.sh` `TEST_TARGETS` gains `m6_sdk` once
  all five tests are green (mirroring the pattern in #129 / PR #130).
- No new build flags. Wrappers reuse the pinned toolchain container from
  §3 / §6.1; `os-pack` reuses the `tools/sof_wrap` exec-bit / rebuild
  discipline from #101 / PR #104 so the regressions tracked in #90 / #91
  don't recur.
- Negative coverage: every M2–M5 acceptance test (#82, #83, #84, #85,
  #118) must continue to pass unchanged. M6 must not require any deny
  to be relaxed.

## Open Questions for the Execute Issue
- **Signing key story for external authors.** Proposal: `os-pack`
  defaults to the development signing key shipped with the SDK (same one
  CI uses), and accepts `--key <path>` for production. Long-term key
  distribution / revocation is explicitly a later milestone.
- **Header stability vs. `kernel/user/` churn.** Proposal: `sdk/include/`
  is generated from a curated subset of `kernel/include/` headers via a
  small script in `build/scripts/sdk_export_headers.sh`. The script
  enforces an allow-list so a kernel-internal header cannot leak into
  the SDK by accident. If a header is on the allow-list, any breaking
  change to it requires an `OS_ABI_VERSION` major bump in the same PR.
- **Where does `samples/` live in CI?** Proposal: `samples/` is built by
  `m6_sdk` tests but is not part of the kernel build graph; it cannot
  break a kernel-only PR's CI signal.
- **Should `os-run` support a non-QEMU host-fake mode for fast iteration?**
  Proposal: no in M6; that's a follow-up SDK-DX issue.

## Exit Criteria
- A follow-up "Execute plan: M6 public SDK + external app template" issue
  is filed referencing this plan.
- The five acceptance tests above are agreed as the merge gate for that
  execute issue.
- No code changes land in this issue (#136).

## Notes
- Plan length deliberately ≤ ~250 lines, matching the shape of
  `2026-05-13-ownership-graph-cascading-deletion.md` (#118 / PR #121).
- Cross-links: this plan must stay consistent with the ABI reference
  (#93 / PR #97), the BearSSL vendor pattern (#117 / PR #122), the
  ownership-cascade contract (#118), and the re-sign discipline (#138).
  If any of those land in a shape that conflicts with the assumptions
  above, the execute issue should adjust this plan rather than silently
  diverge.
