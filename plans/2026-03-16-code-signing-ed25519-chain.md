# Implementation Plan: Code Signing with Ed25519 Certificate Chain

[Overview]
Add code signing validation to SecureOS so that all SOF binaries are verified against a baked-in root Ed25519 public key before execution.

SecureOS currently wraps all executables and libraries in the SOF container format (`kernel/format/sof.h`), which already reserves fields for signatures (`sig_offset`, `sig_size`) and metadata keys for signature algorithm/key ID/hash (`SOF_META_SIG_ALGO`, `SOF_META_SIG_KEYID`, `SOF_META_SIG_HASH`). However, `sof_verify_signature()` is currently a stub that always returns `SOF_OK`, and `sof_build()` always writes `sig_offset=0, sig_size=0`.

This plan implements:
1. **Ed25519 cryptographic primitives** — a freestanding Ed25519 verification library for the kernel (no libc dependency) and a signing library for host tools.
2. **Lightweight certificate chain format** — a SecureOS-specific certificate structure (`secureos_cert_t`) that chains `root → intermediate → binary`. The root public key is embedded in the kernel as `core_root_key`. Intermediate certificates contain a subject public key signed by the root. SOF binaries are signed by intermediate keys.
3. **SOF format extension** — the signature section of SOF files now contains: the intermediate certificate + the Ed25519 signature over the payload hash. New SOF metadata entries identify the signing algorithm and key.
4. **Runtime enforcement** — `process_run()` and `process_load_library()` check whether a binary is signed. If unsigned, a warning is emitted via the console and the user is prompted (y/n) through the existing capability/authorization consent flow. If signed, the certificate chain is validated against the baked-in root key. Invalid signatures cause execution to be blocked.
5. **Build-time signing** — `sof_wrap` is extended with `--sign` flags to sign binaries using a private key file. A `tools/keygen` utility generates root and intermediate key pairs. Build scripts are updated to sign all OS binaries.
6. **Kernel-built binaries** — `fs_service.c` builds SOF binaries at boot time for built-in OS commands. These will be signed using a compile-time-embedded intermediate private key (acceptable since these are first-party OS binaries).

[Types]
New types for Ed25519 crypto, certificate format, and signature verification results.

### Ed25519 Types (`kernel/crypto/ed25519.h`)
```c
/* Ed25519 key sizes */
enum {
  ED25519_PUBLIC_KEY_SIZE = 32,
  ED25519_PRIVATE_KEY_SIZE = 64,  /* expanded private key */
  ED25519_SEED_SIZE = 32,
  ED25519_SIGNATURE_SIZE = 64,
  ED25519_SHA512_HASH_SIZE = 64,
};

/* Result codes */
typedef enum {
  ED25519_OK = 0,
  ED25519_ERR_INVALID_KEY = 1,
  ED25519_ERR_INVALID_SIGNATURE = 2,
  ED25519_ERR_VERIFY_FAILED = 3,
} ed25519_result_t;
```

### Certificate Types (`kernel/crypto/cert.h`)
```c
enum {
  SECUREOS_CERT_MAGIC_SIZE = 4,      /* "SCRT" */
  SECUREOS_CERT_KEY_HASH_SIZE = 32,  /* SHA-512 truncated to 32 bytes */
  SECUREOS_CERT_TOTAL_SIZE = 132,    /* 4 + 32 + 32 + 64 = 132 bytes */
  SECUREOS_CERT_MAX_CHAIN_DEPTH = 4,
};

typedef struct {
  uint8_t magic[4];                                /* "SCRT" = {0x53, 0x43, 0x52, 0x54} */
  uint8_t issuer_key_hash[SECUREOS_CERT_KEY_HASH_SIZE]; /* SHA-512/256 of issuer public key */
  uint8_t subject_public_key[ED25519_PUBLIC_KEY_SIZE];   /* Ed25519 public key of this cert */
  uint8_t signature[ED25519_SIGNATURE_SIZE];              /* Ed25519 signature by issuer over (magic + issuer_key_hash + subject_public_key) */
} secureos_cert_t;

typedef enum {
  CERT_OK = 0,
  CERT_ERR_INVALID_MAGIC = 1,
  CERT_ERR_INVALID_FORMAT = 2,
  CERT_ERR_CHAIN_TOO_DEEP = 3,
  CERT_ERR_ISSUER_MISMATCH = 4,
  CERT_ERR_SIGNATURE_INVALID = 5,
  CERT_ERR_NOT_TRUSTED = 6,
} cert_result_t;
```

### SOF Signature Section Layout
The signature section appended to the SOF file contains:
```
[secureos_cert_t - 132 bytes] [ed25519_signature - 64 bytes over SHA-512 of payload]
```
Total default signature section size: 196 bytes.

### Extended SOF Result Code
```c
SOF_ERR_SIGNATURE_INVALID = 9,  /* Signature present but verification failed */
```

### Extended Capability ID
```c
CAP_CODESIGN_BYPASS = 11,  /* Allows running unsigned binaries without prompt */
```

### Extended Process Result
```c
PROCESS_ERR_SIGNATURE = 9,  /* Signature validation failed */
```

[Files]
New and modified files for the code signing implementation.

### New Files
- `kernel/crypto/ed25519.h` — Ed25519 type definitions and function prototypes
- `kernel/crypto/ed25519.c` — Freestanding Ed25519 verification implementation (based on ref10/TweetNaCl-style, adapted for no-libc)
- `kernel/crypto/sha512.h` — SHA-512 hash function prototypes (needed by Ed25519)
- `kernel/crypto/sha512.c` — Freestanding SHA-512 implementation
- `kernel/crypto/cert.h` — SecureOS certificate chain type definitions and function prototypes
- `kernel/crypto/cert.c` — Certificate parsing, chain validation, and root key trust anchor
- `kernel/crypto/root_key.h` — Baked-in root Ed25519 public key (32-byte C array, auto-generated)
- `tools/keygen/main.c` — Host-side key generation tool (generates root keypair + intermediate cert)
- `tools/keygen/Makefile` — Build for keygen tool
- `tests/ed25519_test.c` — Unit tests for Ed25519 verify
- `tests/cert_chain_test.c` — Unit tests for certificate chain validation
- `tests/codesign_test.c` — Integration tests for SOF signature verification in process loading
- `build/scripts/test_ed25519.sh` — Test runner for Ed25519 tests
- `build/scripts/test_cert_chain.sh` — Test runner for cert chain tests
- `build/scripts/test_codesign.sh` — Test runner for code signing integration tests
- `build/scripts/generate_keys.sh` — Script to generate root + intermediate keys for build

### Modified Files
- `kernel/format/sof.h` — Add `SOF_ERR_SIGNATURE_INVALID` result code; update `sof_verify_signature()` prototype to accept root key
- `kernel/format/sof.c` — Implement real `sof_verify_signature()` that extracts cert + signature from the sig section, validates the cert chain, and verifies the payload signature; update `sof_build()` to accept optional signing parameters
- `kernel/cap/capability.h` — Add `CAP_CODESIGN_BYPASS = 11` to `capability_id_t` enum
- `kernel/cap/cap_table.c` — Update `CAP_ID_MAX` to include new capability
- `kernel/user/process.h` — Add `PROCESS_ERR_SIGNATURE = 9` to `process_result_t`; add `process_codesign_prompt_fn` callback type to `process_context_t`
- `kernel/user/process.c` — Add signature validation before execution in `process_run()` and `process_load_library()`; warn+prompt for unsigned; block for invalid signature
- `kernel/core/console.c` — Add codesign prompt handler; wire into process_context_t; add "unsigned binary" consent flow
- `kernel/core/kmain.c` — No changes needed (built-in binaries signed at compile time via fs_service)
- `kernel/fs/fs_service.c` — Update `fs_build_sof_binary()` and `fs_build_sof_library()` to sign built-in binaries using an embedded intermediate private key
- `tools/sof_wrap/main.c` — Add `--sign-key <key_file>` and `--sign-cert <cert_file>` flags to sign SOF binaries during build
- `tools/sof_wrap/Makefile` — Add ed25519.c, sha512.c, cert.c to compilation
- `build/scripts/build_user_app.sh` — Pass signing key/cert to sof_wrap
- `build/scripts/build_user_lib.sh` — Pass signing key/cert to sof_wrap
- `build/scripts/build_user_app.ps1` — Pass signing key/cert to sof_wrap (Windows)
- `build/scripts/build_user_lib.ps1` — Pass signing key/cert to sof_wrap (Windows)
- `build/scripts/test.sh` — Add `ed25519`, `cert_chain`, `codesign` test targets
- `build/scripts/test.ps1` — Add corresponding Windows test targets
- `build/scripts/test_app_runtime.sh` — Update compilation to include crypto source files
- `tests/app_runtime_test.c` — Add codesign prompt stub to process_context_t; update to handle new PROCESS_ERR_SIGNATURE
- `tests/sof_format_test.c` — Add tests for signed SOF build/parse/verify round-trip

[Functions]
New and modified functions across the codebase.

### New Functions

**`kernel/crypto/sha512.c`**
- `void sha512_init(sha512_ctx_t *ctx)` — Initialize SHA-512 context
- `void sha512_update(sha512_ctx_t *ctx, const uint8_t *data, size_t len)` — Feed data
- `void sha512_final(sha512_ctx_t *ctx, uint8_t out[64])` — Produce 64-byte hash
- `void sha512_hash(const uint8_t *data, size_t len, uint8_t out[64])` — One-shot convenience

**`kernel/crypto/ed25519.c`**
- `ed25519_result_t ed25519_verify(const uint8_t signature[64], const uint8_t *message, size_t message_len, const uint8_t public_key[32])` — Verify an Ed25519 signature
- `void ed25519_public_key_hash(const uint8_t public_key[32], uint8_t out_hash[32])` — SHA-512 of public key truncated to 32 bytes (for cert issuer matching)

**`kernel/crypto/cert.c`**
- `cert_result_t cert_parse(const uint8_t *data, size_t data_len, secureos_cert_t *out)` — Parse raw bytes into a certificate struct
- `cert_result_t cert_verify(const secureos_cert_t *cert, const uint8_t issuer_public_key[32])` — Verify cert signature against issuer key
- `cert_result_t cert_chain_validate(const secureos_cert_t *cert, const uint8_t root_public_key[32])` — Validate cert chains to the embedded root key
- `int cert_is_root_self_signed(const secureos_cert_t *cert, const uint8_t root_public_key[32])` — Check if cert is a self-signed root
- `const uint8_t *cert_get_root_public_key(void)` — Return pointer to the baked-in root public key

**`kernel/format/sof.c` — updated functions**
- `sof_result_t sof_verify_signature(const uint8_t *data, size_t data_len, const sof_parsed_file_t *parsed)` — Real implementation: extract cert from sig section, validate chain, verify Ed25519 signature over payload hash

**`kernel/format/sof.c` — new functions**
- `sof_result_t sof_build_signed(const sof_build_params_t *params, const uint8_t *signing_key, size_t signing_key_len, const uint8_t *cert_data, size_t cert_data_len, uint8_t *out_buffer, size_t out_buffer_size, size_t *out_total_size)` — Build a signed SOF file

**`kernel/user/process.c` — new internal functions**
- `static process_result_t app_validate_signature(const sof_parsed_file_t *parsed, const uint8_t *raw_data, size_t raw_len, const process_context_t *context)` — Orchestrates: check if signed → if yes, verify → if no, prompt user

**`kernel/core/console.c` — new function**
- `static cap_access_state_t console_authorize_unsigned_binary(const char *binary_path)` — Prompts user: "[codesign] WARNING: unsigned binary <path>. Allow execution? (y/n)"

**`tools/keygen/main.c`**
- `int main(int argc, char **argv)` — Generate root keypair and/or intermediate cert signed by root
- `static void keygen_generate_root(const char *out_pub, const char *out_priv)` — Write root key files
- `static void keygen_generate_intermediate(const char *root_priv, const char *out_pub, const char *out_priv, const char *out_cert)` — Generate intermediate keypair + cert signed by root
- `static void keygen_export_root_header(const char *root_pub, const char *out_header)` — Generate `root_key.h` C header

### Modified Functions

**`kernel/format/sof.c`**
- `sof_verify_signature()` — Change from stub (always returns SOF_OK) to real implementation that validates Ed25519 signature and cert chain
- `sof_build()` — No signature changes (stays unsigned); new `sof_build_signed()` added alongside

**`kernel/user/process.c`**
- `process_run()` — After SOF parsing, call `app_validate_signature()` before ELF extraction. If validation returns `PROCESS_ERR_SIGNATURE`, block execution. If unsigned and user denies, block.
- `process_load_library()` — Same signature validation logic added after SOF parsing

**`kernel/fs/fs_service.c`**
- `fs_build_sof_binary()` — Change to call `sof_build_signed()` with embedded intermediate key+cert instead of `sof_build()`
- `fs_build_sof_library()` — Same change for library building

**`tools/sof_wrap/main.c`**
- `main()` — Add `--sign-key` and `--sign-cert` argument parsing; call `sof_build_signed()` when signing params provided

[Classes]
No classes — this is a C codebase. All new abstractions are structs and functions.

The primary new data structures are:
- `secureos_cert_t` (132 bytes) — Certificate containing issuer hash, subject public key, and issuer's signature
- `sha512_ctx_t` — SHA-512 hashing context (internal state for streaming hash)
- `sof_sign_params_t` — Parameters for signed SOF building (signing key + cert blob)

[Dependencies]
No external dependencies are added. All cryptographic code is implemented from scratch in freestanding C.

The Ed25519 implementation will be based on the public-domain TweetNaCl/ref10 algorithm, adapted for freestanding use (no libc, no malloc). SHA-512 will be implemented per FIPS 180-4. Both are well-documented algorithms with public-domain reference implementations available.

**Host-side tools** (`keygen`, `sof_wrap`) will use the same freestanding Ed25519/SHA-512 code compiled for the host, avoiding any OpenSSL dependency. Key files are stored as raw 32-byte or 64-byte binary blobs.

Build dependencies remain unchanged — only `cc` (or `clang`) is needed to compile the crypto code.

[Testing]
Comprehensive testing at three levels: crypto primitives, certificate chain validation, and integration with process loading.

### New Test Files

**`tests/ed25519_test.c`** — compiled by `build/scripts/test_ed25519.sh`
- Test Ed25519 verify with known-good test vectors (RFC 8032 test vectors)
- Test rejection of corrupted signatures (flipped bit)
- Test rejection of wrong public key
- Test rejection of modified message
- Test SHA-512 against known test vectors (NIST examples)

**`tests/cert_chain_test.c`** — compiled by `build/scripts/test_cert_chain.sh`
- Test cert_parse with valid cert bytes
- Test cert_parse rejection of bad magic
- Test cert_parse rejection of truncated data
- Test cert_verify with valid issuer key
- Test cert_verify rejection with wrong issuer key
- Test cert_chain_validate success: root → intermediate cert validates
- Test cert_chain_validate rejection: intermediate signed by unknown key
- Test that baked-in root key is accessible via `cert_get_root_public_key()`

**`tests/codesign_test.c`** — compiled by `build/scripts/test_codesign.sh`
- Build a signed SOF binary using `sof_build_signed()`, parse it, verify signature succeeds
- Build an unsigned SOF binary, verify `sof_signature_present()` returns 0
- Build a signed SOF binary, corrupt the signature, verify `sof_verify_signature()` returns `SOF_ERR_SIGNATURE_INVALID`
- Build a signed SOF binary with wrong cert, verify chain validation fails
- Test `process_run()` with signed binary succeeds
- Test `process_run()` with unsigned binary triggers warning prompt
- Test `process_run()` with invalid signature returns `PROCESS_ERR_SIGNATURE`
- Test `process_load_library()` with signature validation

### Modified Test Files

**`tests/sof_format_test.c`**
- Update `test_sof_signature_stub` to test real signature verification
- Add `test_sof_signed_round_trip` — build signed, parse, verify
- Add `test_sof_signed_corrupt_rejected` — corrupt sig bytes, verify fails

**`tests/app_runtime_test.c`**
- Add `codesign_prompt` callback to test `process_context_t`
- Add test for running signed binary succeeds without prompt
- Add test for running unsigned binary returns expected prompt behavior

### Test Build Scripts

**`build/scripts/test_ed25519.sh`**
```bash
cc -std=c11 -Wall -Wextra -Werror \
  kernel/crypto/sha512.c \
  kernel/crypto/ed25519.c \
  tests/ed25519_test.c \
  -o artifacts/tests/ed25519_test
./artifacts/tests/ed25519_test
```

**`build/scripts/test_cert_chain.sh`**
```bash
cc -std=c11 -Wall -Wextra -Werror \
  kernel/crypto/sha512.c \
  kernel/crypto/ed25519.c \
  kernel/crypto/cert.c \
  tests/cert_chain_test.c \
  -o artifacts/tests/cert_chain_test
./artifacts/tests/cert_chain_test
```

**`build/scripts/test_codesign.sh`**
```bash
cc -std=c11 -Wall -Wextra -Werror \
  kernel/crypto/sha512.c \
  kernel/crypto/ed25519.c \
  kernel/crypto/cert.c \
  kernel/format/sof.c \
  kernel/cap/cap_table.c \
  kernel/hal/storage_hal.c \
  kernel/drivers/disk/ramdisk.c \
  kernel/fs/fs_service.c \
  kernel/user/process.c \
  tests/codesign_test.c \
  -o artifacts/tests/codesign_test
./artifacts/tests/codesign_test
```

[Implementation Order]
Implement in dependency order: crypto primitives → certificate chain → SOF signing → runtime enforcement → build integration → tests.

1. **SHA-512 implementation** (`kernel/crypto/sha512.h`, `kernel/crypto/sha512.c`) — Freestanding SHA-512. This is a dependency for Ed25519. Validate with NIST test vectors.

2. **Ed25519 verification** (`kernel/crypto/ed25519.h`, `kernel/crypto/ed25519.c`) — Freestanding Ed25519 signature verification. Requires SHA-512. Validate with RFC 8032 test vectors.

3. **Ed25519 unit tests** (`tests/ed25519_test.c`, `build/scripts/test_ed25519.sh`) — Verify both SHA-512 and Ed25519 work correctly before building on them.

4. **Certificate format and chain validation** (`kernel/crypto/cert.h`, `kernel/crypto/cert.c`) — Parse `secureos_cert_t`, validate certificate chain from intermediate to root. Depends on Ed25519 verify.

5. **Root key generation and embedding** (`tools/keygen/main.c`, `tools/keygen/Makefile`, `kernel/crypto/root_key.h`, `build/scripts/generate_keys.sh`) — Host-side tool to generate root + intermediate keypairs. Exports root public key as C header. Generates intermediate cert signed by root.

6. **Certificate chain unit tests** (`tests/cert_chain_test.c`, `build/scripts/test_cert_chain.sh`) — Validate cert parsing and chain verification.

7. **SOF format signing extension** — Update `kernel/format/sof.h` with new result codes and signing parameters. Implement `sof_build_signed()` in `kernel/format/sof.c`. Update `sof_verify_signature()` from stub to real verification. Add `SOF_ERR_SIGNATURE_INVALID`.

8. **SOF signing tests** — Update `tests/sof_format_test.c` with signed round-trip and corruption tests.

9. **Capability system extension** — Add `CAP_CODESIGN_BYPASS` to `kernel/cap/capability.h`. Update `CAP_ID_MAX` in `kernel/cap/cap_table.c`.

10. **Process loading enforcement** — Update `kernel/user/process.h` with `PROCESS_ERR_SIGNATURE` and codesign prompt callback. Update `kernel/user/process.c` `process_run()` and `process_load_library()` to validate signatures, warn on unsigned, block on invalid.

11. **Console codesign prompt** — Update `kernel/core/console.c` to implement the unsigned binary consent prompt and wire it into the process context.

12. **Kernel-built binary signing** — Update `kernel/fs/fs_service.c` to sign built-in OS binaries using embedded intermediate key at boot time.

13. **sof_wrap signing support** — Update `tools/sof_wrap/main.c` and `Makefile` to support `--sign-key` and `--sign-cert` flags.

14. **Build script updates** — Update `build/scripts/build_user_app.sh`, `build_user_lib.sh`, and their `.ps1` counterparts to pass signing key/cert to sof_wrap.

15. **Integration tests** (`tests/codesign_test.c`, `build/scripts/test_codesign.sh`) — End-to-end signed binary loading, unsigned binary prompt, invalid signature rejection.

16. **Update app_runtime_test.c** — Add codesign prompt callback, update existing tests for signature awareness.

17. **Update test.sh and test.ps1** — Add ed25519, cert_chain, codesign test targets.