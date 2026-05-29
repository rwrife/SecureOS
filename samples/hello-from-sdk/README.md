# `hello-from-sdk` — minimal external-app sample

Status: **source skeleton only** (slice of M6-SDK-003 / issue
[#396](https://github.com/rwrife/SecureOS/issues/396)). The
`os-cc` / `os-pack` / `os-run` host wrappers and the
`sdk_external_build_isolation` acceptance test that copies this
directory to a scratch dir outside the repo and builds it from there
are **not yet shipped** — they are gated on the A/B design decision
called out in the body of #396 and will be added by the wrapper
slice. Until then this directory is read-only documentation of the
target shape of an external SDK app.

## What's here

- `main.c` — calls `os_console_write("hello from sdk\n")` and returns 0.
  Verbatim from plan §"Sample External App: `hello-from-sdk`"
  ([`plans/2026-05-15-public-sdk-external-app-template.md`](../../plans/2026-05-15-public-sdk-external-app-template.md)).
- `manifest.json` — manifest_version 0 / os_abi_version 0; declares
  `CAP_CONSOLE_WRITE` as required and nothing else. Validated against
  `manifests/schema/v0.json` (see `tools/validate_manifests.py
  samples/hello-from-sdk/manifest.json`).

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

## Not built by default

This directory is **not** wired into `build/scripts/build.sh`,
`build/scripts/test.sh`, or `TEST_TARGETS` in
`build/scripts/validate_bundle.sh`. It is consumed only by the future
`sdk_external_build_isolation` test driver. Adding the sample to the
default build today would defeat the "scratch-dir outside the repo"
isolation guarantee the plan requires.
