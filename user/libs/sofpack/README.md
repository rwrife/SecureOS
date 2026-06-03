# user/libs/sofpack — userland SOF container packer

> **Owner:** in-OS toolchain (M7) / SDK runtime
> **Status:** Sub-slice of [M7-TOOLCHAIN-006 / #409](https://github.com/rwrife/SecureOS/issues/409): freestanding userland-callable SOF wrapper. The matching `cc` driver app half of #409 is still blocked on the TinyCC port ([#408](https://github.com/rwrife/SecureOS/issues/408)).
> **Plan:** [`plans/2026-05-28-in-os-toolchain-self-hosting.md`](../../../plans/2026-05-28-in-os-toolchain-self-hosting.md) (Phase 5)

## What this is

`libsofpack` is the freestanding userland-callable factoring of the SOF
container builder that today lives in `kernel/format/sof.c` (`sof_build`)
and `tools/sof_wrap` (the host CLI). The eventual in-OS `cc` driver
(#409) takes a raw x86_64 ELF from libtcc (#408) and must wrap it as a
`SEOS` container before writing it to `/apps/...` — without dragging the
kernel + crypto headers into userland.

This package provides:

- `sofpack_wrap_size()` — compute the exact byte length the wrapped
  container will occupy, so drivers can size the write buffer.
- `sofpack_wrap()` — wrap a raw ELF payload into an **unsigned** SOF
  container. Byte-for-byte identical to `sof_build()` for the same
  parameters; pinned by `tests/sofpack_wrap_test.c`.

The bytes produced parse cleanly through `sof_parse()` in
`kernel/format/sof.c` and present the same TLV ordering. Wire compat
is the test contract.

## What this is **not**

- Not a signed-path encoder. SOF signing stays in
  `tools/sof_wrap` for build-time signed artifacts; in-OS `cc` output is
  unsigned by construction. The launcher's `AUTH_TYPE_UNSIGNED_BIN`
  prompt (M7-TOOLCHAIN-007 / [#410](https://github.com/rwrife/SecureOS/issues/410))
  is the trust gate for unsigned in-OS builds — sofpack does not need to
  reach into the Ed25519 / root-key surface.
- Not an `.app` bundle packer. APP is reserved-only on the kernel side
  (`sof_parse_app_bundle()` is a documented stub), and sofpack rejects
  `SOFPACK_TYPE_*` values outside `{BIN, LIB}`.

## Layout

```
user/libs/sofpack/
├── include/sofpack/
│   └── sofpack.h        # public API (BIN/LIB types, result codes, build params)
├── src/
│   └── sofpack.c        # freestanding implementation; no kernel includes
└── README.md
```

The implementation depends only on `<stddef.h>` / `<stdint.h>` (already
shipped by `user/libs/clib`) — no host libc, no crypto, no kernel
internals.

## Tests

`tests/sofpack_wrap_test.c` is the host unit test. It links against
`kernel/format/sof.c` (and the crypto stack it pulls in) so the wire
equivalence with `sof_build()` is asserted byte-for-byte. The test
dispatches via:

```
build/scripts/test.sh sofpack_wrap
```

and is gated by the bundle through `validate_bundle.sh` (so a regression
in either encoder flips the bundle to FAIL).

Covered today:

- `sofpack_wrap_size()` agrees with `sofpack_wrap()`.
- Byte-identical to `sof_build()` for matching params (with and without
  optional `icon`/`syscall_id` TLVs).
- Round-trip through `sof_parse()`: type, payload bytes, metadata recovery.
- NULL/missing params, missing payload, invalid file_type rejection.
- Buffer-too-small rejection.
- Long-value (>255 byte) clamp parity with `sof_build`.

## Wire-compat contract

The on-disk header + TLV layout is documented in `src/sofpack.c` and
mirrors `kernel/format/sof.c` exactly. The `byte_identical_to_sof_build`
arm of the host test is the load-bearing assertion: if either encoder
ever drifts, that arm flips the bundle red.

## Follow-ups (issue #409 remainder)

- **`cc` driver app** (`user/apps/cc/`) — blocked on the libtcc port
  (#408). Once a `tcc_compile()` entry point lands, the driver will:
  1. read input source via `os_fs_*`,
  2. invoke `tcc_compile()` to emit an in-memory ELF,
  3. call `sofpack_wrap()` to produce the SOF container,
  4. write the result via `os_fs_*`.
- **On-target build target** — `build/scripts/build_user_lib.sh sofpack`
  (wired into `build.sh build_libs()`'s archive-only branch) produces
  `artifacts/user/libs/libsofpack.a` next to the existing SOF-wrapped
  libs. The future `cc` driver app (#409) will pick it up from there;
  today the only consumer is the host link-pin test.
