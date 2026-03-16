# Implementation Plan

[Overview]
Introduce the SecureOS File Format (SOF) — a container format wrapping ELF payloads with typed metadata and code-signing stubs — and migrate the entire codebase from raw `.elf` files to `.bin` (binaries) and `.lib` (libraries).

SecureOS currently loads and executes raw ELF32 binaries directly from the in-memory filesystem. Files are distinguished only by directory convention (`/os/`, `/apps/` for executables, `/lib/` for libraries) and all share the `.elf` extension. There is no embedded metadata (name, author, version, etc.) and no provision for code signing.

This plan introduces a new container format that prepends a structured binary header and metadata section to the existing ELF payload. The three file categories are:

- **Binary (`.bin`)** — Executables intended for end-user interaction with a robust `main()` entry point. Stored in `/os/` (system commands) and `/apps/` (user applications).
- **Library (`.lib`)** — Shared code consumed by other binaries and libraries. Stored in `/lib/`.
- **Data** — Anything that is not a binary or library. No special format; plain files like `readme.txt` are unchanged.

The SOF header includes reserved fields for digital signature offset, size, and algorithm. These are populated with zeroes in this iteration, enabling future code-signing enforcement without format changes. A capability `CAP_APP_EXEC` enforcement point will be added; the future signing verification will integrate at the same gate.

All buffer sizes will be increased from 512 to 1024 bytes to accommodate the header overhead, and the filesystem's single-cluster write limit will be raised to support 2-cluster (1024-byte) files.

The current process execution system lives entirely in `kernel/user/process.c` and `kernel/user/process.h`. The older `app_runtime.c`, `app_runtime.h`, `process_launcher.c`, and `process_launcher.h` files have been removed and consolidated into `process.c/.h`. All modifications target these two files.

[Types]
New types define the SOF binary format, metadata key identifiers, parsing results, and in-memory representations for the container header and metadata entries.

### `sof_file_type_t` (enum, `kernel/format/sof.h`)
```c
typedef enum {
  SOF_TYPE_INVALID = 0x00,
  SOF_TYPE_BIN     = 0x01,  /* Executable binary */
  SOF_TYPE_LIB     = 0x02,  /* Library */
  SOF_TYPE_APP     = 0x03,  /* Application bundle (reserved, not yet implemented) */
} sof_file_type_t;
```

### `sof_sig_algorithm_t` (enum, `kernel/format/sof.h`)
```c
typedef enum {
  SOF_SIG_NONE     = 0x00,  /* Unsigned */
  SOF_SIG_ED25519  = 0x01,  /* Reserved for future */
  SOF_SIG_RSA2048  = 0x02,  /* Reserved for future */
} sof_sig_algorithm_t;
```

### `sof_meta_key_t` (enum, `kernel/format/sof.h`)
```c
typedef enum {
  SOF_META_NAME        = 0x01,
  SOF_META_DESCRIPTION = 0x02,
  SOF_META_AUTHOR      = 0x03,
  SOF_META_VERSION     = 0x04,
  SOF_META_DATE        = 0x05,
  SOF_META_ICON        = 0x06,
  SOF_META_SIG_ALGO    = 0x10,  /* Future: signature algorithm name */
  SOF_META_SIG_KEYID   = 0x11,  /* Future: signing key identifier */
  SOF_META_SIG_HASH    = 0x12,  /* Future: payload hash algorithm */
} sof_meta_key_t;
```

### `sof_header_t` (struct, `kernel/format/sof.h`)
Binary on-disk header — 32 bytes, all little-endian:
```c
typedef struct {
  uint8_t  magic[4];           /* "SEOS" = {0x53,0x45,0x4F,0x53} */
  uint8_t  format_version;     /* 1 */
  uint8_t  file_type;          /* sof_file_type_t */
  uint16_t flags;              /* Reserved, 0 */
  uint32_t total_size;         /* Total file size in bytes */
  uint32_t meta_offset;        /* Byte offset to metadata section */
  uint16_t meta_count;         /* Number of metadata TLV entries */
  uint16_t meta_size;          /* Total size of metadata section in bytes */
  uint32_t payload_offset;     /* Byte offset to ELF payload */
  uint32_t payload_size;       /* Size of ELF payload in bytes */
  uint32_t sig_offset;         /* Byte offset to signature (0 = unsigned) */
  uint32_t sig_size;           /* Size of signature (0 = unsigned) */
} __attribute__((packed)) sof_header_t;
```
Static assert: `sizeof(sof_header_t) == 32`

### Metadata TLV on-disk entry format
Each metadata entry on disk is:
```
  uint8_t  key_id;      /* sof_meta_key_t */
  uint8_t  value_len;   /* 0–255 */
  char     value[];     /* value_len bytes, NOT null-terminated */
```
Total entry size = 2 + value_len.

### `sof_meta_entry_t` (struct, `kernel/format/sof.h`)
In-memory representation of a single parsed metadata entry:
```c
enum { SOF_META_VALUE_MAX = 64 };

typedef struct {
  sof_meta_key_t key;
  uint8_t value_len;
  char value[SOF_META_VALUE_MAX];  /* null-terminated copy */
} sof_meta_entry_t;
```

### `sof_result_t` (enum, `kernel/format/sof.h`)
```c
typedef enum {
  SOF_OK = 0,
  SOF_ERR_INVALID_MAGIC = 1,
  SOF_ERR_INVALID_VERSION = 2,
  SOF_ERR_INVALID_TYPE = 3,
  SOF_ERR_INVALID_SIZE = 4,
  SOF_ERR_INVALID_META = 5,
  SOF_ERR_BUFFER_TOO_SMALL = 6,
  SOF_ERR_NO_PAYLOAD = 7,
  SOF_ERR_SIGNATURE_REQUIRED = 8,  /* Future: unsigned file rejected */
} sof_result_t;
```

### `sof_parsed_file_t` (struct, `kernel/format/sof.h`)
Result of parsing a SOF file from a raw byte buffer:
```c
enum { SOF_META_MAX_ENTRIES = 12 };

typedef struct {
  sof_header_t header;
  sof_meta_entry_t meta[SOF_META_MAX_ENTRIES];
  size_t meta_count;
  const uint8_t *payload;      /* Pointer into source buffer */
  size_t payload_size;
  int has_signature;           /* 0 = unsigned, 1 = signed (future) */
} sof_parsed_file_t;
```

### `sof_build_params_t` (struct, `kernel/format/sof.h`)
Parameters for building a SOF file in memory:
```c
typedef struct {
  sof_file_type_t file_type;
  const char *name;
  const char *description;
  const char *author;
  const char *version;
  const char *date;
  const char *icon;            /* May be NULL */
  const uint8_t *elf_payload;
  size_t elf_payload_size;
} sof_build_params_t;
```

### `sof_app_bundle_header_t` (struct, `kernel/format/sof.h`) — **RESERVED / STUB**
Reserved for future `.app` bundle support. An `.app` file is a lightly compressed archive containing `.bin`, `.lib`, and data files. Executing an `.app` will extract and launch its binaries/libraries and mount a virtual HAL disk for data file access. This struct is declared but not used in this iteration.
```c
typedef struct {
  uint32_t entry_count;        /* Number of entries in the bundle (future) */
  uint32_t manifest_offset;    /* Offset to bundle manifest (future) */
  uint32_t manifest_size;      /* Size of bundle manifest (future) */
  uint32_t compression_algo;   /* 0=NONE, future: 1=LZ4, 2=ZSTD */
  uint32_t reserved[4];        /* Reserved for future use */
} sof_app_bundle_header_t;
```

### Updated enums/constants
- `APP_FILE_MAX` in `kernel/user/process.c`: increase from 512 to 1024
- `FS_ELF_BUFFER_MAX` in `kernel/fs/fs_service.c`: rename to `FS_SOF_BUFFER_MAX`, increase from 512 to 1024
- New capability `CAP_APP_EXEC = 10` in `kernel/cap/capability.h` (stub for future signing gate)

[Files]
New and modified files spanning the format library, kernel loader, filesystem, build pipeline, and tests.

### New Files

1. **`kernel/format/sof.h`** — SOF format type definitions, struct declarations, parsing/building function prototypes. All types listed in the [Types] section are declared here.

2. **`kernel/format/sof.c`** — Implementation of SOF parsing and building:
   - `sof_parse()` — Validates magic, version, type; extracts metadata TLV entries; returns payload pointer.
   - `sof_validate_header()` — Checks magic bytes, version, type field, size consistency.
   - `sof_get_meta()` — Retrieves a metadata entry by key from a parsed file.
   - `sof_build()` — Constructs a complete SOF file in a provided buffer from `sof_build_params_t`.
   - `sof_is_sof()` — Quick check: does a buffer start with "SEOS" magic?
   - `sof_signature_present()` — Returns whether signature fields are populated (future stub, always returns 0).
   - `sof_verify_signature()` — Stub that always returns `SOF_OK` (future: will verify digital signatures).

3. **`tests/sof_format_test.c`** — Unit tests for the SOF format library:
   - Build a SOF binary, parse it back, verify all metadata fields.
   - Build a SOF library, parse it back, verify type is LIB.
   - Reject truncated files, wrong magic, wrong version.
   - Verify signature stub returns OK.
   - Test round-trip: build → parse → extract payload matches original.

4. **`build/scripts/test_sof_format.sh`** — Shell test runner for `sof_format_test.c`. Compiles the test against `kernel/format/sof.c` and runs it, following the pattern of existing test scripts.

5. **`build/scripts/test_sof_format.ps1`** — PowerShell wrapper that invokes `test_sof_format.sh` inside the Docker toolchain container.

6. **`tools/sof_wrap/main.c`** — Host-side CLI tool that wraps a raw ELF file into SOF format. Usage: `sof_wrap --type bin --name "app" --author "SecureOS" --version "1.0.0" --date "2026-03-16" input.elf output.bin`. Compiles natively inside the Docker toolchain and is used by the build scripts.

7. **`tools/sof_wrap/Makefile`** — Simple Makefile to compile `sof_wrap` from `main.c` and `kernel/format/sof.c`.

### Modified Files

8. **`kernel/user/process.c`** — Major changes (this is the sole process execution file; `app_runtime.c` and `process_launcher.c` no longer exist):
   - Replace `app_parse_elf_program()` with `app_parse_sof_program()` that first calls `sof_parse()`, validates the file type (BIN vs LIB), then extracts the ELF payload and delegates to the existing ELF program-header parser.
   - Update `process_run()` to reject files that are not `SOF_TYPE_BIN`.
   - Update `process_load_library()` to reject files that are not `SOF_TYPE_LIB`.
   - Add `sof_verify_signature()` call as a stub check point (always passes for now).
   - Change all `.elf` string literals to `.bin` or `.lib` as appropriate.
   - Rename `app_append_elf_suffix()` → `app_append_bin_suffix()`. Change the suffix check and append from `.elf` to `.bin`.
   - Update `app_build_library_path()` to use `.lib` extension instead of `.elf`.
   - Increase `APP_FILE_MAX` from 512 to 1024.

9. **`kernel/user/process.h`** — Add `#include "../format/sof.h"` for SOF type access. No structural changes to existing types.

10. **`kernel/fs/fs_service.c`** — Major changes:
    - Rename `fs_build_script_elf()` → `fs_build_sof_binary()`. This function will: (a) build the ELF wrapper around the script as before, (b) wrap the result in a SOF container with metadata using `sof_build()`.
    - Add `fs_build_sof_library()` for library SOF files.
    - Update `fs_service_init()`: change all `.elf` references to `.bin` for executables and `.lib` for libraries.
    - Rename `FS_ELF_BUFFER_MAX` → `FS_SOF_BUFFER_MAX` and increase to 1024.
    - Increase `fs_write_entry_content()` limit to support > 512 bytes (up to 1024 with 2-cluster chain).
    - Add `fs_read_file_bytes_extended()` or modify `fs_read_file_bytes()` to follow FAT cluster chains for multi-cluster reads (needed for files > 512 bytes).

11. **`kernel/fs/fs_service.h`** — No public API changes needed; internal buffer size is private.

12. **`kernel/cap/capability.h`** — Add `CAP_APP_EXEC = 10` to the `capability_id_t` enum. This capability will gate execution of SOF binaries (currently auto-granted; future: requires signature verification).

13. **`build/scripts/build_user_app.ps1`** — Change output extension from `.elf` to `.bin`. Add SOF wrapping step: after `ld.lld` produces the ELF, run `sof_wrap --type bin` to produce the final `.bin` file.

14. **`build/scripts/build_user_app.sh`** — Same changes as `.ps1` version.

15. **`build/scripts/build_user_lib.ps1`** — Change output extension from `.elf` to `.lib`. Add SOF wrapping step with `--type lib`.

16. **`build/scripts/build_user_lib.sh`** — Same changes as `.ps1` version.

17. **`build/scripts/build.ps1`** — Add `sof-wrap` build target. Ensure `sof_wrap` tool is built before user apps/libs.

18. **`build/scripts/build.sh`** — Same changes as `.ps1` version.

19. **`build/scripts/validate_bundle.sh`** — Add `sof_format` to the `TEST_TARGETS` array.

20. **`tests/app_runtime_test.c`** — Update all `.elf` string references to `.bin`/`.lib`. The test still exercises `process_run()`, `process_list_apps()`, `process_list_libraries()`, and `process_load_library()` from `kernel/user/process.h` but expects SOF-wrapped files from `fs_service_init()`. Grant `CAP_APP_EXEC` to the test subject.

21. **`tests/fs_service_test.c`** — Update any `.elf` references to `.bin`/`.lib`.

22. **`kernel/core/kmain.c`** — Grant `CAP_APP_EXEC` to bootstrap subjects alongside existing capabilities.

### Files NOT changed
- `kernel/user/app_runtime.c` — **Deleted in prior refactor.** Does not exist.
- `kernel/user/app_runtime.h` — **Deleted in prior refactor.** Does not exist.
- `kernel/user/process_launcher.c` — **Deleted in prior refactor.** Does not exist.
- `kernel/user/process_launcher.h` — **Deleted in prior refactor.** Does not exist.
- `user/apps/*/main.c` — User-space C source files are unchanged; only the build output format changes.
- `user/libs/*/main.c` — Same; source unchanged, build output changes.
- `user/include/secureos_api.h` — No changes.

[Functions]
New functions for the SOF format library, and modifications to existing loader and filesystem functions.

### New Functions

**In `kernel/format/sof.c`:**

1. `sof_result_t sof_parse(const uint8_t *data, size_t data_len, sof_parsed_file_t *out)` — Parse a raw byte buffer into a `sof_parsed_file_t`. Validates magic, version, type, size fields. Extracts all metadata TLV entries. Sets `out->payload` to point into the source buffer at `payload_offset`.

2. `sof_result_t sof_validate_header(const uint8_t *data, size_t data_len)` — Quick validation of just the 32-byte header without full metadata parsing.

3. `int sof_is_sof(const uint8_t *data, size_t data_len)` — Returns 1 if the first 4 bytes match "SEOS" magic and data_len >= 32, 0 otherwise.

4. `sof_result_t sof_get_meta(const sof_parsed_file_t *parsed, sof_meta_key_t key, const char **out_value, size_t *out_len)` — Look up a metadata entry by key. Returns pointer to the null-terminated value string in the parsed struct. Returns `SOF_ERR_INVALID_META` if key not found.

5. `sof_result_t sof_build(const sof_build_params_t *params, uint8_t *out_buffer, size_t out_buffer_size, size_t *out_total_size)` — Construct a complete SOF file. Writes header, metadata TLV entries, payload into `out_buffer`. Sets signature fields to 0. Returns `SOF_ERR_BUFFER_TOO_SMALL` if the buffer is insufficient.

6. `int sof_signature_present(const sof_header_t *header)` — Returns 1 if `sig_offset != 0 && sig_size != 0`, 0 otherwise. Currently always returns 0 for newly built files.

7. `sof_result_t sof_verify_signature(const uint8_t *data, size_t data_len, const sof_parsed_file_t *parsed)` — Stub. Always returns `SOF_OK`. Future: will verify the digital signature against the payload hash using the algorithm specified in metadata.

8. `sof_result_t sof_parse_app_bundle(const uint8_t *data, size_t data_len, sof_parsed_file_t *out)` — **STUB.** Reserved for future `.app` bundle parsing. Validates that the SOF header has `file_type == SOF_TYPE_APP`, then returns `SOF_ERR_INVALID_TYPE` with an error indicating bundles are not yet supported. This placeholder ensures the function signature is established for forward compatibility.

**In `kernel/fs/fs_service.c`:**

8. `static fs_result_t fs_build_sof_binary(const char *script, const char *name, uint8_t *out_buffer, size_t out_buffer_size, size_t *out_len)` — Replaces `fs_build_script_elf()`. Builds a minimal ELF wrapper around the script text, then wraps it in a SOF container with `SOF_TYPE_BIN`, name, author="SecureOS", version="1.0.0", date="2026-03-16", description matching the name.

9. `static fs_result_t fs_build_sof_library(const char *script, const char *name, uint8_t *out_buffer, size_t out_buffer_size, size_t *out_len)` — Same as above but with `SOF_TYPE_LIB`.

**In `kernel/user/process.c`:**

10. `static process_result_t app_parse_sof_program(const uint8_t *file_data, size_t file_len, sof_file_type_t expected_type, const uint8_t **out_program, size_t *out_program_len)` — New top-level parser replacing direct calls to `app_parse_elf_program()`. Calls `sof_parse()` to extract the SOF container, validates the file type matches `expected_type`, calls `sof_verify_signature()` (stub), then delegates to the existing `app_parse_elf_program()` to extract the LOAD segment from the ELF payload.

**In `tools/sof_wrap/main.c`:**

11. `int main(int argc, char **argv)` — CLI entry point. Parses command-line arguments (`--type`, `--name`, `--author`, `--version`, `--date`, `--description`, `--icon`), reads the input ELF file, calls `sof_build()`, writes the output SOF file.

### Modified Functions

**In `kernel/user/process.c`:**

12. `process_run()` — Change the call from `app_parse_elf_program(elf_data, elf_len, &program, &program_len)` to `app_parse_sof_program(elf_data, elf_len, SOF_TYPE_BIN, &program, &program_len)`.

13. `process_load_library()` — Change the call from `app_parse_elf_program()` to `app_parse_sof_program(... SOF_TYPE_LIB ...)`.

14. `app_append_elf_suffix()` — Rename to `app_append_bin_suffix()`. Change the suffix check and append from `.elf` to `.bin`.

15. `app_build_library_path()` — Change `.elf` extension checks/appends to `.lib`.

16. `app_build_path_from_dir()` — Calls `app_append_elf_suffix()` which becomes `app_append_bin_suffix()`.

17. `app_path_is_library()` — No signature change, logic unchanged.

**In `kernel/fs/fs_service.c`:**

18. `fs_build_script_elf()` — Removed. Replaced by `fs_build_sof_binary()`. The internal ELF-building logic is preserved as a static helper `fs_build_elf_wrapper()` called by the new function.

19. `fs_service_init()` — All `fs_write_file_bytes("os/help.elf", ...)` calls become `fs_write_file_bytes("os/help.bin", ...)`. All `lib/*.elf` become `lib/*.lib`. All `apps/*.elf` become `apps/*.bin`. Uses `fs_build_sof_binary()` and `fs_build_sof_library()` instead of `fs_build_script_elf()`.

20. `fs_write_entry_content()` — Increase the single-write limit from `FS_BLOCK_SIZE` (512) to `FS_BLOCK_SIZE * 2` (1024). Add cluster-chain allocation for writes that exceed one cluster.

21. `fs_read_file_bytes()` — Add support for reading multi-cluster files by following the FAT chain. Currently reads only a single cluster.

### Removed Functions

22. `fs_build_script_elf()` in `kernel/fs/fs_service.c` — Replaced by `fs_build_sof_binary()`. The internal ELF construction logic is preserved as the static helper `fs_build_elf_wrapper()`.

[Classes]
No classes exist in this C codebase. All abstractions are expressed through structs, enums, and function groups as described in [Types] and [Functions].

[Dependencies]
No new external dependencies are required; all code is freestanding C compiled with the existing toolchain.

The SOF format library (`kernel/format/sof.c`) uses only `<stdint.h>` and `<stddef.h>` from freestanding headers. The `sof_wrap` host tool additionally uses `<stdio.h>`, `<stdlib.h>`, and `<string.h>` from the hosted C library available in the Docker toolchain container.

The existing toolchain image (`secureos/toolchain:bookworm-2026-02-12`) includes `clang`, `ld.lld`, and standard POSIX utilities, which are sufficient for building the `sof_wrap` tool.

No new Docker image changes or package installations are needed.

[Testing]
New test file for SOF format validation plus updates to existing tests that reference `.elf` extensions.

### New Test File

**`tests/sof_format_test.c`** — Comprehensive test suite for the SOF format library:

1. **`test_sof_build_and_parse_bin`** — Build a SOF BIN file with known metadata and a small ELF payload. Parse it back and verify: magic, version, type, all metadata keys/values, payload pointer and size match the original ELF.

2. **`test_sof_build_and_parse_lib`** — Same as above but with `SOF_TYPE_LIB`.

3. **`test_sof_is_sof`** — Verify `sof_is_sof()` returns 1 for valid SOF data and 0 for raw ELF data or random bytes.

4. **`test_sof_reject_bad_magic`** — Modify magic bytes in a valid SOF buffer; verify `sof_parse()` returns `SOF_ERR_INVALID_MAGIC`.

5. **`test_sof_reject_bad_version`** — Set format version to 99; verify `SOF_ERR_INVALID_VERSION`.

6. **`test_sof_reject_truncated`** — Pass a buffer shorter than 32 bytes; verify `SOF_ERR_INVALID_SIZE`.

7. **`test_sof_reject_bad_type`** — Set file type to 0xFF; verify `SOF_ERR_INVALID_TYPE`.

8. **`test_sof_get_meta_found`** — Parse a valid SOF file and retrieve each metadata key; verify values match.

9. **`test_sof_get_meta_not_found`** — Query for a key that was not included; verify `SOF_ERR_INVALID_META`.

10. **`test_sof_signature_stub`** — Verify `sof_signature_present()` returns 0 and `sof_verify_signature()` returns `SOF_OK` for a newly built file.

11. **`test_sof_round_trip_payload`** — Build a SOF file, parse it, confirm the extracted payload bytes match the original input byte-for-byte.

### Test Runner

**`build/scripts/test_sof_format.sh`** — Compiles `tests/sof_format_test.c` together with `kernel/format/sof.c` using the Docker toolchain, then runs the resulting binary. Follows the same pattern as `test_app_runtime.sh`.

**`build/scripts/test_sof_format.ps1`** — PowerShell wrapper that calls `test_sof_format.sh` inside the Docker container.

### Modified Tests

**`tests/app_runtime_test.c`** (tests `process_run()` and related functions from `kernel/user/process.h`):
- All string literals referencing `.elf` change to `.bin` or `.lib` (e.g., `"help.elf"` → `"help.bin"`, `"envlib.elf"` → `"envlib.lib"`).
- The `fs_write_file_bytes("tools/toolping.elf", ...)` call changes to `"tools/toolping.bin"`.
- The `fs_write_file_bytes("sandbox/ping.elf", ...)` call changes to `"sandbox/ping.bin"`.
- Library path assertions change from `"/lib/envlib.elf"` to `"/lib/envlib.lib"`.
- Grant `CAP_APP_EXEC` to the test subject alongside existing capabilities.

**`tests/fs_service_test.c`**:
- Any references to `.elf` files in filesystem assertions change to `.bin`/`.lib`.

**`build/scripts/validate_bundle.sh`**:
- Add `sof_format` to the `TEST_TARGETS` array.

[Implementation Order]
Implement bottom-up: format library first, then kernel integration, then build pipeline, then tests.

1. **Create `kernel/format/sof.h`** — Define all SOF types, enums, structs, and function prototypes as specified in [Types]. This is the foundational header that all other changes depend on.

2. **Create `kernel/format/sof.c`** — Implement `sof_parse()`, `sof_validate_header()`, `sof_is_sof()`, `sof_get_meta()`, `sof_build()`, `sof_signature_present()`, `sof_verify_signature()`. Include proper file-level comment block per AGENTS.md rules.

3. **Create `tests/sof_format_test.c`** and **`build/scripts/test_sof_format.sh`** / **`build/scripts/test_sof_format.ps1`** — Write and run the SOF format unit tests to validate the format library in isolation before integrating it into the kernel.

4. **Add `CAP_APP_EXEC`** to `kernel/cap/capability.h` — Add the new capability ID to the enum. Update `kernel/core/kmain.c` to grant it to bootstrap subjects.

5. **Update `kernel/fs/fs_service.c`** — Replace `fs_build_script_elf()` with `fs_build_sof_binary()` and add `fs_build_sof_library()`. Update `fs_service_init()` to use `.bin`/`.lib` extensions. Increase `FS_SOF_BUFFER_MAX` to 1024. Extend `fs_write_entry_content()` and `fs_read_file_bytes()` to support 2-cluster files.

6. **Update `kernel/user/process.c`** — Add `#include "../format/sof.h"`. Add `app_parse_sof_program()`. Replace `app_parse_elf_program()` calls in `process_run()` and `process_load_library()` with `app_parse_sof_program()`. Rename `app_append_elf_suffix()` → `app_append_bin_suffix()`. Update `app_build_library_path()` to use `.lib`. Update all `.elf` string literals to `.bin`/`.lib`. Increase `APP_FILE_MAX` to 1024.

7. **Update `kernel/user/process.h`** — Add `#include "../format/sof.h"`.

8. **Update tests** — Modify `tests/app_runtime_test.c` and `tests/fs_service_test.c` to use `.bin`/`.lib` extensions and grant `CAP_APP_EXEC`. Add `sof_format` to `validate_bundle.sh` test targets.

9. **Create `tools/sof_wrap/main.c`** and **`tools/sof_wrap/Makefile`** — Build the host-side SOF wrapping tool for the build pipeline.

10. **Update build scripts** — Modify `build_user_app.ps1/.sh` and `build_user_lib.ps1/.sh` to compile `sof_wrap`, then use it to wrap ELF output into `.bin`/`.lib` SOF files. Update `build.ps1/.sh` to add the `sof-wrap` build target.

11. **Run all tests** — Execute the full test suite via `validate_bundle.sh` to confirm no regressions: `sof_format`, `app_runtime`, `fs_service`, and all other existing tests pass.

---

## Future: `.app` Bundle Support

The `.app` bundle format is **not implemented in this plan** but is accounted for via reserved types and stubs. A future plan will cover:

- **Bundle archive format** — A lightly compressed archive (LZ4 or ZSTD) containing multiple SOF `.bin`, `.lib`, and data files, plus a JSON or binary manifest describing entry points, dependencies, and data file paths.
- **Bundle manifest** — Declares which `.bin` is the main entry point, which `.lib` files to preload, and data file mount points.
- **Virtual HAL disk** — Executing a `.app` creates an isolated virtual disk (via the storage HAL layer) that exposes the bundle's data files to the running process. The process sees a sandboxed filesystem containing only its own data.
- **Process launcher integration** — `process_run()` will detect `SOF_TYPE_APP`, extract the bundle, mount the virtual disk, load libraries, and execute the entry binary.
- **Compression** — `sof_app_bundle_header_t.compression_algo` will select the decompression algorithm. Initially `0 = NONE` (uncompressed tar-like archive), with LZ4/ZSTD support added later.

Reserved artifacts in this plan:
- `SOF_TYPE_APP = 0x03` in `sof_file_type_t`
- `sof_app_bundle_header_t` struct declaration in `sof.h`
- `sof_parse_app_bundle()` stub function in `sof.c` (returns `SOF_ERR_INVALID_TYPE`)
