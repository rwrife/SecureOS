# SecureOS File Format (SOF) Implementation Plan

**Date:** 2026-03-16  
**Status:** Implemented

## Overview

Introduces the SecureOS File Format (SOF) — a container format wrapping ELF
payloads with typed metadata and code-signing stubs — and migrates the entire
codebase from raw `.elf` files to `.bin` (binaries) and `.lib` (libraries).

## SOF Container Format

The SOF format consists of:

1. **Header** (36 bytes, packed, little-endian) — magic "SEOS", format version,
   file type, flags, total size, metadata offset/count/size, payload offset/size,
   signature offset/size.

2. **Metadata section** — TLV (tag-length-value) entries with 1-byte key ID,
   1-byte value length, and N-byte value. Supported keys: NAME, DESCRIPTION,
   AUTHOR, VERSION, DATE, ICON, plus reserved SIG_ALGO, SIG_KEYID, SIG_HASH.

3. **ELF payload** — The original ELF32 binary.

4. **Signature stub** — Reserved fields (sig_offset=0, sig_size=0 for unsigned).

## File Types

| Type | Extension | Enum Value | Description |
|------|-----------|------------|-------------|
| Binary | `.bin` | SOF_TYPE_BIN (0x01) | Executables in `/os/` and `/apps/` |
| Library | `.lib` | SOF_TYPE_LIB (0x02) | Shared code in `/lib/` |
| App Bundle | `.app` | SOF_TYPE_APP (0x03) | Reserved for future bundle support |

## Key Changes

### New Files
- `kernel/format/sof.h` — SOF type definitions, structs, function prototypes
- `kernel/format/sof.c` — SOF parsing, building, validation, signature stubs
- `tests/sof_format_test.c` — Unit tests for the SOF format library
- `build/scripts/test_sof_format.sh` — Test runner for SOF format tests
- `tools/sof_wrap/main.c` — Host-side CLI tool for wrapping ELF → SOF
- `tools/sof_wrap/Makefile` — Build file for sof_wrap tool

### Modified Files
- `kernel/cap/capability.h` — Added `CAP_APP_EXEC = 10`
- `kernel/core/kmain.c` — Grants `CAP_APP_EXEC` to bootstrap subjects
- `kernel/fs/fs_service.c` — SOF wrapping functions, multi-cluster write/read,
  all `.elf` → `.bin`/`.lib` filenames
- `kernel/user/process.c` — SOF parsing before ELF extraction, `.bin`/`.lib`
  extensions, increased `APP_FILE_MAX` to 1024
- `kernel/user/process.h` — Includes `sof.h`
- `tests/app_runtime_test.c` — Updated `.elf` → `.bin`/`.lib` references
- `tests/fs_service_test.c` — Updated `.elf` → `.bin`/`.lib` references
- `build/scripts/test.sh` / `test.ps1` — Added `sof_format` test target
- `build/scripts/test_app_runtime.sh` — Includes `sof.c` in compilation
- `build/scripts/test_fs_service.sh` — Includes `sof.c` in compilation
- `build/scripts/validate_bundle.sh` — Added `sof_format` to test targets
- `build/scripts/build_user_app.sh` / `.ps1` — SOF wrapping step after ELF link
- `build/scripts/build_user_lib.sh` / `.ps1` — SOF wrapping step after ELF link

## Buffer Size Changes
- `APP_FILE_MAX`: 512 → 1024 (kernel/user/process.c)
- `FS_ELF_BUFFER_MAX` → `FS_SOF_BUFFER_MAX`: 512 → 1024 (kernel/fs/fs_service.c)
- `fs_write_entry_content()`: supports up to 2-cluster (1024 byte) writes
- `fs_read_file_bytes()`: supports multi-cluster reads via FAT chain following

## Code Signing Stubs
- `sof_verify_signature()` always returns `SOF_OK`
- `sof_signature_present()` returns 0 for all newly built files
- Future: will verify ED25519/RSA2048 signatures against payload hashes

## Future: .app Bundle Support
- `SOF_TYPE_APP = 0x03` reserved
- `sof_app_bundle_header_t` struct declared
- `sof_parse_app_bundle()` stub returns `SOF_ERR_INVALID_TYPE`
- Full implementation deferred to a future plan

## Testing
- `tests/sof_format_test.c` — 11 test cases covering build/parse round-trips,
  metadata lookup, error rejection, signature stubs, and payload integrity
- All existing tests updated for `.bin`/`.lib` extensions