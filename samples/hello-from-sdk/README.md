# `hello-from-sdk` — minimal external-app sample

Status: **starter implementation in progress** for
[#584](https://github.com/rwrife/SecureOS/issues/584). The final
`os-cc` / `os-pack` / `os-run` wrapper flow is still pending
[#396](https://github.com/rwrife/SecureOS/issues/396), but this
sample now has a host-side build + manifest gate (`m6_sample_hello_from_sdk`)
so SDK-surface drift is caught before wrapper wiring lands.

## What's here

- `main.c` — calls `os_console_write("hello from sdk\n")` and returns 0.
  Verbatim from plan §"Sample External App: `hello-from-sdk`"
  ([`plans/2026-05-15-public-sdk-external-app-template.md`](../../plans/2026-05-15-public-sdk-external-app-template.md)).
- `manifest.json` — manifest_version 0 / os_abi_version 0, explicit
  `owner.kind: "external"`, and `CAP_CONSOLE_WRITE` as the required
  capability. Validated against `manifests/schema/v0.json` by
  `build/scripts/test_m6_sample_hello_from_sdk.sh`.

## Containment rules this sample obeys

The plan requires the future `sdk_external_build_isolation` test to
prove that the wrappers do not silently rely on in-tree paths. To make
that test honest, this sample's source file must already be SDK-only at
the include level — even though no CI build yet copies it to a scratch
dir:

- Headers under `kernel/` are forbidden (already CI-enforced for the
  `sdk/` tree itself by `validate_sdk_no_kernel_includes.sh`).
- Headers under `user/include/` are forbidden — the SDK is the external
  ABI surface; `user/include/secureos_api.h` is in-tree-only. The
  wrapper slice's `sdk_external_build_isolation` test will enforce this
  by building from a scratch directory with `-I sdk/include` only.
- Only `os/*` SDK headers (and a forward declaration of
  `os_console_write` until `sdk/include/os/console.h` lands in the
  wrapper slice) are referenced.

## When the wrappers land

Once #396 (or its split sub-issues) ships the host wrappers, the
build-and-run dance from a fresh copy will be:

```bash
cp -r /path/to/SecureOS/samples/hello-from-sdk /tmp/x
cd /tmp/x
os-cc main.c -o hello-from-sdk.elf
os-pack hello-from-sdk.elf manifest.json -o hello-from-sdk.sof
os-run hello-from-sdk.sof
```

…with no `-I` / `-L` reaching back into the source tree.

## Current execution scope

This sample now has a host-gated test target:

```bash
./build/scripts/test.sh m6_sample_hello_from_sdk
```

The full wrapper + QEMU execution path remains intentionally deferred:

```bash
./build/scripts/test.sh m6_sample_hello_from_sdk_qemu
# -> TEST:SKIP:m6_sample_hello_from_sdk_qemu:os_cc_build:wrappers_unwired_pending_issue_396
```

That SKIP marker is deliberate issue-tracking signal, not a silent pass.
