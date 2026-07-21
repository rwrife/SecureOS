# SecureOS File Format (SOF) Container Wire Layout

Status: PINNED at `OS_ABI_VERSION = 0` (format_version = 1)
Owner: kernel/format (`kernel/format/sof.c` / `kernel/format/sof.h`)
Originally specified by: M2 launcher work; documented here as the
first slice of the in-OS toolchain SOF doc gap called out in
[#521](https://github.com/rwrife/SecureOS/issues/521) (done-when bullet
5: "docs/abi/sof-format.md authoritative wire description").

## Why this exists

SOF (`SEOS`-magic) is the on-disk container every executable (`.bin`)
and library (`.lib`) in SecureOS ships in. It wraps a raw x86_64 ELF
payload with a 36-byte fixed header, a TLV metadata section, and an
optional Ed25519 signature trailer. It is the wire surface across
three trust boundaries:

- the host-side packer in `tools/sof_wrap/` that signs build-time
  artifacts against `secureos-dev-key-1`;
- the kernel parser/verifier in `kernel/format/sof.c` that the launcher
  (`kernel/user/launcher_exec.c`) calls before extracting the ELF;
- the freestanding userland packer in `user/libs/sofpack/` (M7-TOOLCHAIN-006,
  [#409](https://github.com/rwrife/SecureOS/issues/409)) that the in-OS
  `cc` driver will use to wrap libtcc output without dragging the kernel
  crypto stack into userland.

All three must agree byte-for-byte on the layout below. The byte
equivalence between `sof_build()` in the kernel and `sofpack_wrap()` in
userland is pinned by `tests/sofpack_wrap_test.c`. Any change to this
document, the layout, or the enums below MUST move in lockstep with the
header in `kernel/format/sof.h`, the parser/builder in
`kernel/format/sof.c`, and the userland encoder in
`user/libs/sofpack/src/sofpack.c`.

## At-a-glance layout

```
+----------------------------------+  offset 0
|        sof_header_t (36)         |
+----------------------------------+  offset = meta_offset      (== 36)
|   metadata TLV section           |
|   meta_count entries, packed     |
|   total bytes == meta_size       |
+----------------------------------+  offset = payload_offset
|   raw x86_64 ELF payload         |
|   payload_size bytes             |
+----------------------------------+  offset = sig_offset       (0 if unsigned)
|   secureos_cert_t (132)          |
|   Ed25519 signature  (64)        |   sig_size == 196 when present
+----------------------------------+  offset = total_size
```

The header records every section offset/length, so consumers parse the
header first and seek directly to metadata, payload, or signature
without scanning. There is **no inter-section alignment padding**: the
metadata section starts immediately after the header at offset 36, the
payload starts immediately after the last TLV, and the signature (if
present) starts immediately after the payload.

All multi-byte integer fields are **little-endian**.

## File header (`sof_header_t`, 36 bytes, packed LE)

The header is pinned at exactly 36 bytes by the
`_Static_assert(sizeof(sof_header_t) == 36, ...)` in
`kernel/format/sof.h`. Layout, in declaration order:

| Off | Size | Field            | Notes |
| --- | ---- | ---------------- | ----- |
| 0   | 4    | `magic[4]`       | `"SEOS"` = `{0x53, 0x45, 0x4F, 0x53}`. `sof_is_sof()` accepts only this byte sequence. |
| 4   | 1    | `format_version` | `1` for this spec. Any other value parses as `SOF_ERR_INVALID_VERSION`. Bumping this is a wire-format break and would require an `OS_ABI_VERSION` review per `docs/abi/versioning.md`. |
| 5   | 1    | `file_type`      | `sof_file_type_t`: `0x01 SOF_TYPE_BIN`, `0x02 SOF_TYPE_LIB`. `0x00 SOF_TYPE_INVALID` and `0x03 SOF_TYPE_APP` (reserved bundle slot) both parse as `SOF_ERR_INVALID_TYPE` today; `sof_parse_app_bundle()` is a documented stub. |
| 6   | 2    | `flags`          | Reserved, MUST be `0`. The parser does not reject non-zero values today, but any non-zero value is undefined and may be rejected in future format_versions. |
| 8   | 4    | `total_size`     | Total file size in bytes, including header + metadata + payload + signature (if any). Parser rejects files where `total_size > data_len`. |
| 12  | 4    | `meta_offset`    | Byte offset to the metadata TLV section. Currently always `36` (immediately after the header) for both encoders; parsers MUST honour the value rather than hard-coding 36. |
| 16  | 2    | `meta_count`     | Number of metadata TLV entries. Capped at `SOF_META_MAX_ENTRIES = 12` by the in-memory parser; entries past the cap are silently ignored by `sof_parse` but the on-wire layout permits the full `uint16` range. |
| 18  | 2    | `meta_size`      | Total bytes in the metadata section. Parser rejects when `meta_offset + meta_size > data_len`. |
| 20  | 4    | `payload_offset` | Byte offset to the ELF payload. Both encoders emit `payload_offset == meta_offset + meta_size` (no gap). |
| 24  | 4    | `payload_size`   | ELF payload byte length. Parser rejects when `payload_offset + payload_size > data_len`. `sof_build()` rejects zero-byte payloads with `SOF_ERR_NO_PAYLOAD`. |
| 28  | 4    | `sig_offset`     | Byte offset to the signature section, or `0` if the file is unsigned. `sof_signature_present()` returns true iff both `sig_offset != 0` and `sig_size != 0`. |
| 32  | 4    | `sig_size`       | Signature section byte length, or `0` if unsigned. When present, `sig_size == sizeof(secureos_cert_t) + 64 = 132 + 64 = 196`. |

### Magic and version

```
magic           = 0x53 0x45 0x4F 0x53          ("SEOS")
format_version  = 0x01
```

`sof_is_sof(data, len)` is the cheapest accept test (returns 1 iff
`len >= 32` and the first four bytes are `SEOS`); `sof_validate_header()`
is the equivalent quick check that also enforces version/type/size
invariants without parsing metadata.

## Metadata TLV section

The metadata section is a packed sequence of `meta_count` TLV entries.
Each entry is:

```
+-----------+--------+--------+---/ /---+
| key (u8)  | len(u8)|  value (len bytes, NOT null-terminated on wire)
+-----------+--------+--------+---/ /---+
```

- `key` (1 byte): one of `sof_meta_key_t` (see below).
- `len` (1 byte): value byte length. The encoder clamps any source
  string longer than 255 chars to 255 on wire (so `len` always fits in
  one byte); this clamp is exercised by `tests/sofpack_wrap_test.c`'s
  long-value parity case.
- `value` (`len` bytes): UTF-8 / ASCII text, no leading length prefix,
  **no NUL terminator on wire**. The in-memory parser copies the
  bytes into a NUL-terminated buffer (`sof_meta_entry_t::value`, capped
  at `SOF_META_VALUE_MAX = 64` chars + NUL).

Entries appear **in fixed declaration order** when emitted by
`sof_build()` / `sofpack_wrap()`:

1. `SOF_META_NAME` (`0x01`)
2. `SOF_META_DESCRIPTION` (`0x02`)
3. `SOF_META_AUTHOR` (`0x03`)
4. `SOF_META_VERSION` (`0x04`)
5. `SOF_META_DATE` (`0x05`)
6. `SOF_META_ICON` (`0x06`) — optional, emitted only when non-NULL
7. `SOF_META_SYSCALL_ID` (`0x20`) — optional, emitted only when non-NULL

Reserved-but-not-yet-emitted keys (parsed when present, not produced by
either encoder today):

- `SOF_META_SIG_ALGO` (`0x10`)
- `SOF_META_SIG_KEYID` (`0x11`)
- `SOF_META_SIG_HASH` (`0x12`)

Consumers MUST tolerate unknown keys (skip via `len`) for forward
compatibility within `format_version = 1`. The parser today silently
drops entries past `SOF_META_MAX_ENTRIES = 12`; in-memory entries
beyond that are inaccessible via `sof_get_meta()` but the on-wire
section is still walked end-to-end for the `meta_size` invariant.

The section's on-wire length is the sum of the per-entry sizes
(`2 + len_i` for each entry). `meta_size` MUST equal this sum.

## Payload section

The payload section is a raw x86_64 ELF image, byte-for-byte. The
launcher (`launcher_exec.c`) treats `&data[payload_offset]` as the ELF
load buffer and applies the SecureOS user image-base / entry pinning
documented in [`syscalls.md`](syscalls.md) (`os_process_spawn`). There
is no per-payload compression, alignment, or CRC at this format
version; integrity is delegated to the optional signature trailer.

## Signature trailer (optional)

When `sig_offset != 0 && sig_size != 0`, the bytes at
`[sig_offset, sig_offset + sig_size)` are laid out as:

```
+------------------------------+----------------------+
| secureos_cert_t (132 bytes)  | Ed25519 sig (64 B)   |
+------------------------------+----------------------+
                              sig_size = 196 bytes
```

- `secureos_cert_t` is the SecureOS code-signing certificate format
  (subject, public key, root signature) defined in
  `kernel/crypto/cert.h`.
- The Ed25519 signature covers the SHA-512 hash of the **payload bytes
  only** (`payload_offset .. payload_offset + payload_size`); header
  and metadata are not part of the signed digest at format_version 1.
  `sof_verify_signature()` is the canonical verifier and walks both the
  cert chain (against the baked-in root key) and the payload Ed25519
  signature.
- `sof_build()` (unsigned) always writes `sig_offset = 0` and
  `sig_size = 0`. `sof_build_signed()` is the host-only signed path
  (`tools/sof_wrap/`); in-OS produced binaries are unsigned by
  construction and are gated by the `AUTH_TYPE_UNSIGNED_BIN` launcher
  prompt (M7-TOOLCHAIN-007, [#410](https://github.com/rwrife/SecureOS/issues/410)).

`sof_signature_present()` is the canonical "is this file signed?"
predicate. Unsigned files are not a parse error — they are accepted by
`sof_parse()` and rejected (or prompted on) at the launcher trust gate,
not at the format layer.

## Result codes

`sof_result_t` (`kernel/format/sof.h`) is the format-layer error
surface. Values are stable for the lifetime of `format_version = 1`:

| Code | Symbol | When emitted |
| ---- | ------ | ------------ |
| 0 | `SOF_OK` | Parse / build / verify succeeded. |
| 1 | `SOF_ERR_INVALID_MAGIC` | First four bytes are not `"SEOS"`, or `data_len < 32`. |
| 2 | `SOF_ERR_INVALID_VERSION` | `format_version != 1`. |
| 3 | `SOF_ERR_INVALID_TYPE` | `file_type` is not `SOF_TYPE_BIN` (0x01) or `SOF_TYPE_LIB` (0x02). Also returned by the `sof_parse_app_bundle()` stub when called against an `APP`-typed header. |
| 4 | `SOF_ERR_INVALID_SIZE` | `total_size > data_len`, or `payload_offset + payload_size > data_len`, or any out-of-bounds offset/length combination. Also returned by `sof_build()` for NULL `params` / `out_buffer` / `out_total_size`. |
| 5 | `SOF_ERR_INVALID_META` | A metadata TLV would read past `meta_offset + meta_size`, or `sof_get_meta()` did not find the requested key. |
| 6 | `SOF_ERR_BUFFER_TOO_SMALL` | `sof_build()` was handed an output buffer smaller than the computed `total_size`. |
| 7 | `SOF_ERR_NO_PAYLOAD` | `sof_build()` was handed `elf_payload == NULL` or `elf_payload_size == 0`. |
| 8 | `SOF_ERR_SIGNATURE_REQUIRED` | Reserved for future signed-only enforcement; not produced by the current parser. |
| 9 | `SOF_ERR_SIGNATURE_INVALID` | Signature section present but Ed25519 / cert-chain verification failed. |

## Compatibility envelope

- `format_version = 1` is the only version this document defines.
  Bumping `format_version` is a wire-format break and follows the
  `OS_ABI_VERSION` major-bump policy in
  [`versioning.md`](versioning.md).
- Machine-readable constant pin: [`sof-format-constants.json`](sof-format-constants.json)
  (validated by `tools/validate_sof_format_constants.py` via the
  `sof_format_constants` test target). Keep this JSON in lockstep with
  `kernel/format/sof.h` and this document.
- Adding a new `SOF_META_*` key is **additive**: keep the existing key
  IDs frozen, allocate a new ID, document it in this file, and update
  both `sof_build()` and `sofpack_wrap()` in the same change.
- Adding a new `sof_file_type_t` value is **additive** *iff* the
  reserved `0x03 SOF_TYPE_APP` slot is consumed first (it is reserved
  by the existing parser stub).
- Adding a new `sof_result_t` value is **additive** so long as existing
  numeric codes do not move; consumers MUST treat unknown codes as a
  generic format-layer failure.

## Consumers / cross-references

- `kernel/format/sof.c` — canonical parser, unsigned builder, verifier.
- `kernel/format/sof.h` — frozen type and enum surface this doc tracks.
- `kernel/user/launcher_exec.c` — launcher path; consumes
  `sof_parse()` + `sof_verify_signature()`.
- `kernel/fs/fs_service.c` — wraps script-generated ELF binaries via
  `sof_build()` at filesystem init.
- `tools/sof_wrap/` — host-side signed packer
  (`sof_build_signed()` consumer).
- `user/libs/sofpack/` — freestanding userland encoder pinned
  byte-for-byte against `sof_build()` (PR
  [#511](https://github.com/rwrife/SecureOS/pull/511) /
  [#409](https://github.com/rwrife/SecureOS/issues/409)).
- [`syscalls.md`](syscalls.md) — `os_process_spawn` consumer trust
  contract for SOF-staged binaries.
- [`versioning.md`](versioning.md) — `OS_ABI_VERSION` / format-version
  bump policy.

Last verified against commit: 8b4124007023626cf4d784620ed130dd3950e246
