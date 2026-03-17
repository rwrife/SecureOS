# Implementation Plan

[Overview]
Add HTTPS (TLS 1.2 client) support to the netlib library using BearSSL as the TLS engine, with a minimal embedded CA trust store and a new capability gate.

SecureOS currently has a complete HTTP/1.1 client stack in `user/libs/netlib/` (DNS → UDP → TCP → HTTP) that explicitly rejects `https://` URLs. The existing crypto infrastructure (Ed25519 + SHA-512) serves code signing well but lacks the primitives needed for TLS: RSA/ECDSA verification, AES symmetric encryption, SHA-256, HMAC, and X.509 certificate parsing.

**Ed25519 vs RSA analysis**: Ed25519 was the correct choice for code signing — it is fast, produces small signatures, and has deterministic signing. RSA would not have replaced Ed25519 for code signing. HTTPS requires RSA and/or ECDSA only for *verifying external server certificates* issued by public Certificate Authorities. These are complementary concerns: Ed25519 for internal trust (code signing), RSA/ECDSA for external trust (TLS server verification). Adding full RSA support at the code-signing stage would not have meaningfully reduced the work needed for HTTPS, since TLS also requires AES, HMAC-SHA256, X.509 parsing, and the full handshake state machine.

**Approach**: Integrate BearSSL (BSD-licensed, designed for embedded/freestanding environments, no malloc required) as a vendored dependency under `vendor/bearssl/`. BearSSL provides all needed primitives: RSA/ECDSA verification, AES-GCM/CBC, SHA-256, HMAC, X.509 certificate chain validation, and a complete TLS 1.2 client engine. The integration adds a new `tls.c` module to netlib that wraps TCP send/recv into BearSSL I/O callbacks, a new `https.c` module that layers on top of `tls.c` the same way `http.c` layers on `tcp.c`, and a minimal embedded CA bundle for server certificate verification.

**Capability model**: HTTPS reuses the existing `CAP_NETWORK` gate — no new capability is needed since HTTPS is just encrypted HTTP. A new `CAP_TLS` could be added in v2 if fine-grained network permission splitting is desired.

[Types]
New types for TLS session management, HTTPS request/response, and CA trust anchors.

### TLS Types (`user/libs/netlib/tls.h`)
```c
#include <stddef.h>
#include <stdint.h>
#include "tcp.h"

typedef enum {
  TLS_OK = 0,
  TLS_ERR_HANDSHAKE  = 1,  /* TLS handshake failed */
  TLS_ERR_CERT       = 2,  /* Server certificate verification failed */
  TLS_ERR_SEND       = 3,  /* Encrypted send failed */
  TLS_ERR_RECV       = 4,  /* Encrypted recv failed or timeout */
  TLS_ERR_CLOSED     = 5,  /* Server closed connection */
  TLS_ERR_NO_MEMORY  = 6,  /* Static buffer exhausted */
} tls_result_t;

enum {
  TLS_IO_BUF_SIZE   = 8192,   /* BearSSL recommended minimum I/O buffer */
  TLS_MAX_CHAIN_LEN = 3,      /* Max server cert chain depth */
  TLS_DEFAULT_PORT  = 443,
};

typedef struct {
  br_ssl_client_context sc;           /* BearSSL client context */
  br_x509_minimal_context xc;        /* BearSSL X.509 validator */
  tcp_conn_t tcp;                     /* Underlying TCP connection */
  uint8_t iobuf[TLS_IO_BUF_SIZE];    /* BearSSL I/O buffer */
  int connected;                      /* 1 if TLS handshake completed */
} tls_conn_t;
```

### HTTPS Types (`user/libs/netlib/https.h`)
```c
#include "http.h"
#include "tls.h"

typedef enum {
  HTTPS_OK             = 0,
  HTTPS_ERR_BAD_URL    = 1,  /* Could not parse URL */
  HTTPS_ERR_DNS        = 2,  /* DNS resolution failed */
  HTTPS_ERR_CONNECT    = 3,  /* TCP connect failed */
  HTTPS_ERR_TLS        = 4,  /* TLS handshake or cert error */
  HTTPS_ERR_SEND       = 5,  /* Failed to transmit request */
  HTTPS_ERR_RECV       = 6,  /* Timeout or connection error on receive */
  HTTPS_ERR_RESPONSE   = 7,  /* Response could not be parsed */
} https_result_t;

/* Uses the same http_request_t and http_response_t structs from http.h */
https_result_t https_request(const http_request_t *req, http_response_t *resp);
```

### CA Trust Anchor Types (`user/libs/netlib/ca_bundle.h`)
```c
#include "vendor/bearssl/inc/bearssl.h"

/* Returns the array of BearSSL trust anchors and sets *count. */
const br_x509_trust_anchor *ca_bundle_get(size_t *count);
```

### Updated URL Parser Output
The existing `http_parsed_url_t` (private in `http.c`) gains a field:
```c
typedef struct {
  char host[128];
  uint16_t port;
  char path[256];
  int valid;
  int is_https;    /* NEW: 1 if url starts with "https://" */
} http_parsed_url_t;
```

### Updated Netlib API Additions (`user/include/lib/netlib.h`)
```c
/* HTTPS GET - same signature pattern as netlib_http_get */
netlib_status_t netlib_https_get(netlib_handle_t handle,
                                 const char *url,
                                 char *out_buffer,
                                 unsigned int out_buffer_size);
```

### Updated OS ABI Addition (`user/include/secureos_api.h`)
```c
os_status_t os_net_https_get(const char *url, char *out_buffer, unsigned int out_buffer_size);
```

[Files]
New files, vendored dependency, and modifications to existing files.

### Vendored Dependency
- `vendor/bearssl/` — Vendored BearSSL source tree. Only the required subset of source files will be compiled:
  - `vendor/bearssl/inc/bearssl.h` and sub-headers (public API)
  - `vendor/bearssl/src/ssl/` — TLS engine (client context, handshake, record layer)  
  - `vendor/bearssl/src/x509/` — X.509 certificate parsing and chain validation
  - `vendor/bearssl/src/rsa/` — RSA signature verification (for server certs)
  - `vendor/bearssl/src/ec/` — ECDSA signature verification (for server certs)
  - `vendor/bearssl/src/hash/` — SHA-256, SHA-1 (required by TLS PRF)
  - `vendor/bearssl/src/mac/` — HMAC
  - `vendor/bearssl/src/aead/` — AES-GCM
  - `vendor/bearssl/src/symcipher/` — AES core
  - `vendor/bearssl/src/codec/` — ASN.1 DER decoder
  - `vendor/bearssl/src/int/` — Big integer arithmetic for RSA
  - `vendor/bearssl/src/rand/` — HMAC-DRBG PRNG (required by TLS client)
  - `vendor/bearssl/src/inner.h` — Internal header

  BearSSL is ~75 `.c` files for a full TLS 1.2 client. A `vendor/bearssl/Makefile.secureos` will list the exact subset.
  BearSSL compiles cleanly in freestanding mode with `-ffreestanding -nostdlib`.

### New Files
| File | Purpose |
|---|---|
| `vendor/bearssl/` | Vendored BearSSL source (BSD license) |
| `vendor/bearssl/Makefile.secureos` | Lists BearSSL source files to compile for SecureOS |
| `vendor/bearssl/secureos_compat.c` | Minimal shims: `memcpy`, `memmove`, `memset`, `memcmp`, `strlen` stubs that BearSSL calls, redirected to freestanding implementations |
| `user/libs/netlib/tls.h` | TLS session types and connect/send/recv/close API |
| `user/libs/netlib/tls.c` | TLS client implementation: wraps BearSSL engine over TCP I/O callbacks |
| `user/libs/netlib/https.h` | HTTPS client request/response types |
| `user/libs/netlib/https.c` | HTTPS/1.1 client: URL parse → DNS → TCP connect → TLS handshake → HTTP over TLS |
| `user/libs/netlib/ca_bundle.h` | Trust anchor accessor function prototype |
| `user/libs/netlib/ca_bundle.c` | Embedded CA root certificates (Let's Encrypt ISRG Root X1, DigiCert Global Root G2, GlobalSign Root CA, Baltimore CyberTrust Root, Amazon Root CA 1 — ~5 CAs in DER format as C byte arrays converted to `br_x509_trust_anchor` structs) |
| `user/libs/netlib/entropy.h` | Entropy/random seed interface for TLS PRNG |
| `user/libs/netlib/entropy.c` | Entropy collection: combines RDTSC (if available), TCP sequence counter, and a static boot-time seed as initial PRNG input |
| `build/scripts/build_bearssl.sh` | Compiles BearSSL objects for i386 freestanding target |
| `build/scripts/build_bearssl.ps1` | Windows equivalent |
| `tests/tls_test.c` | Unit tests for TLS connection setup and teardown (mock TCP) |
| `tests/https_test.c` | Unit tests for HTTPS URL parsing and request formatting |
| `build/scripts/test_tls.sh` | Test runner for TLS unit tests |
| `build/scripts/test_https.sh` | Test runner for HTTPS unit tests |
| `docs/plans/2026-03-17-netlib-https-bearssl.md` | Copy of this plan for project documentation |

### Modified Files
| File | Change |
|---|---|
| `user/libs/netlib/http.c` | Update `http_parse_url()` to recognize `https://` and set `is_https` flag; update `http_request()` to delegate to `https_request()` when `is_https` is set (transparent upgrade) |
| `user/libs/netlib/http.h` | No changes to public API (http_request transparently handles both) |
| `user/libs/netlib/api.c` | Add `netlib_https_get()` implementation; update `netlib_http_get()` to auto-detect `https://` URLs and route through HTTPS path |
| `user/libs/netlib/backend.h` | Add `NETLIB_BACKEND_TLS_IO_BUF` size constant |
| `user/include/lib/netlib.h` | Add `netlib_https_get()` prototype |
| `user/include/secureos_api.h` | Add `os_net_https_get()` ABI placeholder |
| `user/runtime/secureos_api_stubs.c` | Add `os_net_https_get` stub |
| `kernel/user/native_net_service.h` | Add `native_net_https_get()` declaration |
| `kernel/user/native_net_service.c` | Add `native_net_https_get()` implementation that calls netlib HTTPS path |
| `kernel/user/process.c` | Wire `os_net_https_get` syscall to `native_net_https_get()` in native bridge |
| `user/apps/os/http/main.c` | Update to handle `https://` URLs (netlib_http_get already routes transparently, but update usage help text) |
| `user/os_commands/http.cmd` | Update help text to mention HTTPS support |
| `build/scripts/build_kernel_entry.sh` | Add BearSSL object files and new netlib TLS/HTTPS objects to kernel link |
| `build/scripts/build_kernel_entry.ps1` | Same changes for Windows |
| `build/scripts/build_user_lib.sh` | Add BearSSL and TLS/HTTPS sources to netlib shared library build |
| `build/scripts/build_user_lib.ps1` | Same changes for Windows |
| `build/scripts/test.sh` | Add `tls` and `https` test targets |
| `build/scripts/test.ps1` | Same for Windows |
| `build/docker/Dockerfile.toolchain` | Add `curl` or `wget` for fetching BearSSL source during image build (optional — vendored source is committed) |
| `.gitignore` | Ensure `vendor/bearssl/build/` artifacts are ignored |
| `docs/architecture/CAPABILITIES.md` | Note that `CAP_NETWORK` covers HTTPS as well as HTTP |

[Functions]
New and modified functions across the codebase.

### New Functions

**`user/libs/netlib/tls.c`**
- `tls_result_t tls_connect(tls_conn_t *conn, uint32_t remote_ip, uint16_t remote_port, const char *hostname)` — Establish TCP connection, initialize BearSSL client context with CA trust anchors, set expected server name (SNI), perform TLS handshake via polling loop
- `tls_result_t tls_send(tls_conn_t *conn, const uint8_t *data, size_t len)` — Feed plaintext into BearSSL engine, flush encrypted records via TCP send
- `size_t tls_recv(tls_conn_t *conn, uint8_t *buf_out, size_t buf_size, uint32_t timeout)` — Poll TCP for encrypted data, feed into BearSSL engine, return decrypted plaintext
- `void tls_close(tls_conn_t *conn)` — Send TLS close_notify, close TCP connection
- `static int tls_sock_read(void *ctx, unsigned char *buf, size_t len)` — BearSSL low-level read callback: calls `tcp_recv` on the underlying TCP connection
- `static int tls_sock_write(void *ctx, const unsigned char *buf, size_t len)` — BearSSL low-level write callback: calls `tcp_send` on the underlying TCP connection
- `static void tls_init_client(tls_conn_t *conn, const char *hostname)` — Configure BearSSL client context: set x509 minimal validator, load trust anchors from `ca_bundle_get()`, set I/O buffer, set supported cipher suites (TLS_RSA_WITH_AES_128_GCM_SHA256, TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256)

**`user/libs/netlib/https.c`**
- `https_result_t https_request(const http_request_t *req, http_response_t *resp)` — Full HTTPS request flow: parse URL, DNS resolve, TLS connect (with SNI hostname), format HTTP request into plaintext buffer, `tls_send`, `tls_recv`, parse HTTP response, `tls_close`
- `static size_t https_format_request(uint8_t *buf, size_t buf_size, const char *method, const char *host, const char *path, const http_header_t *headers, size_t header_count, const char *body, size_t body_len)` — Build HTTP/1.1 request text (reuses same format as http.c)

**`user/libs/netlib/ca_bundle.c`**
- `const br_x509_trust_anchor *ca_bundle_get(size_t *count)` — Returns pointer to static array of `br_x509_trust_anchor` structs representing the minimal CA bundle; sets `*count` to the number of entries

**`user/libs/netlib/entropy.c`**
- `void entropy_init(void)` — Initialize entropy pool from available sources
- `void entropy_get_seed(uint8_t *out, size_t len)` — Fill buffer with pseudo-random bytes for TLS PRNG seeding
- `static uint32_t entropy_rdtsc_low(void)` — Read low 32 bits of TSC (x86 `rdtsc` instruction via inline asm, with fallback counter for non-x86)

**`vendor/bearssl/secureos_compat.c`**
- `void *memcpy(void *dst, const void *src, size_t n)` — Freestanding memcpy for BearSSL
- `void *memmove(void *dst, const void *src, size_t n)` — Freestanding memmove for BearSSL
- `void *memset(void *s, int c, size_t n)` — Freestanding memset for BearSSL
- `int memcmp(const void *a, const void *b, size_t n)` — Freestanding memcmp for BearSSL
- `size_t strlen(const char *s)` — Freestanding strlen for BearSSL

**`user/libs/netlib/api.c` — new function**
- `netlib_status_t netlib_https_get(netlib_handle_t handle, const char *url, char *out_buffer, unsigned int out_buffer_size)` — HTTPS GET: calls `https_request()`, copies response body to output buffer

### Modified Functions

**`user/libs/netlib/http.c`**
- `http_parse_url()` — Remove the early-return that rejects `https://` URLs. Instead, detect `https://` prefix, set `parsed.is_https = 1`, set `parsed.port = 443` as default, strip scheme and continue parsing host/path normally
- `http_request()` — After URL parsing, check `parsed.is_https`: if set, delegate to `https_request()` and map `https_result_t` to `http_result_t`; if not set, continue with existing TCP path

**`user/libs/netlib/api.c`**
- `netlib_http_get()` — The internal `http_request()` now transparently handles HTTPS, so no change needed here beyond the fact that `http_request` will auto-delegate. However, add explicit `netlib_https_get()` as a named entry point for apps that want to be explicit.

**`kernel/user/native_net_service.c`**
- Add `native_net_https_get()` — Same pattern as `native_net_http_get()` but calls the HTTPS path

**`kernel/user/process.c`**
- `native_app_syscall_dispatch()` (or equivalent syscall bridge) — Add `os_net_https_get` case that routes to `native_net_https_get()`

[Classes]
No classes — this is a C codebase. All new abstractions are structs and functions.

Primary new data structures:
- `tls_conn_t` — TLS session state wrapping BearSSL client context, X.509 validator, TCP connection, and I/O buffer
- `br_x509_trust_anchor` (BearSSL type) — Represents a CA root certificate trust anchor (DN, public key, flags)
- `http_parsed_url_t.is_https` — Extended URL parse result indicating HTTPS scheme

BearSSL itself uses several internal structs (`br_ssl_client_context`, `br_x509_minimal_context`, etc.) that are opaque to our code and managed through BearSSL's API.

[Dependencies]
BearSSL is added as the sole new external dependency.

### BearSSL
- **License**: MIT-like (BSD-style permissive) — compatible with SecureOS
- **Version**: Latest stable (1.0.x branch from https://www.bearssl.org/)
- **Integration method**: Vendored source committed under `vendor/bearssl/`
- **Build method**: Compiled as a set of `.o` files using the same `clang --target=i386-unknown-none-elf -ffreestanding -fno-stack-protector -m32` flags as the rest of the project
- **Runtime requirements**: No heap allocation (BearSSL uses caller-provided buffers), no libc (freestanding compat shims provided in `secureos_compat.c`), no OS threads
- **Code size**: Approximately 80-100KB of code for client-only TLS 1.2 with RSA+ECDSA+AES-GCM
- **RAM usage**: ~8KB for I/O buffer + ~2KB for BearSSL internal state per connection; one connection at a time (matches existing single-connection TCP model)

### CA Certificate Bundle
- 5 root CA certificates in DER format, embedded as C byte arrays:
  1. **ISRG Root X1** (Let's Encrypt) — covers majority of modern HTTPS sites
  2. **DigiCert Global Root G2** — widely used commercial CA
  3. **GlobalSign Root CA** — widely used commercial CA  
  4. **Baltimore CyberTrust Root** — used by Azure/Microsoft services
  5. **Amazon Root CA 1** — used by AWS/CloudFront
- Total embedded size: ~5-7KB for all 5 DER-encoded certificates
- Each cert is converted to a `br_x509_trust_anchor` struct at compile time using BearSSL's `brssl` tool or a custom Python script

### No Other Dependencies
- No changes to the Docker toolchain image beyond what's already available (clang, lld, make)
- No runtime dynamic linking — BearSSL objects are statically linked into both the kernel image and the netlib shared library

[Testing]
Testing at three levels: BearSSL integration, TLS connection, and HTTPS end-to-end.

### New Test Files

**`tests/tls_test.c`** — compiled by `build/scripts/test_tls.sh`
- Test `tls_conn_t` initialization (BearSSL context setup with trust anchors)
- Test that `ca_bundle_get()` returns non-null with count > 0
- Test entropy seed generation returns non-zero bytes
- Test BearSSL compat shims (`memcpy`, `memmove`, `memset`, `memcmp`) work correctly
- Test URL parser correctly identifies `https://` scheme and sets `is_https = 1`, default port 443
- Test URL parser correctly handles `https://host:8443/path` custom port
- Test URL parser still handles `http://` URLs with `is_https = 0`, default port 80

**`tests/https_test.c`** — compiled by `build/scripts/test_https.sh`
- Test HTTPS request formatting produces valid HTTP/1.1 text with Host and Connection headers
- Test `https_request()` with mock TCP that returns a TLS server-hello triggers handshake flow (stub test)
- Test `http_request()` transparently delegates `https://` URLs to the HTTPS path
- Test `http_request()` still works for `http://` URLs via existing TCP path
- Test `netlib_https_get()` with invalid URL returns `NETLIB_STATUS_ERROR`
- Test `netlib_http_get()` with `https://` URL routes through HTTPS transparently

### Modified Test Files

**`tests/app_runtime_test.c`**
- Add `os_net_https_get` stub to the native bridge mock
- Verify that the `http` command app handles `https://` URLs without crashing

### Integration Test (QEMU)
- Manual/CI integration: boot SecureOS in QEMU with user-mode networking, run `http https://example.com/` — verify a response is received (QEMU user-mode networking supports outbound TLS through the host's network stack)
- This test is environment-dependent and will be documented as a manual verification step rather than an automated CI test initially

### Test Build Scripts

**`build/scripts/test_tls.sh`**
```bash
# Compile BearSSL subset + compat + netlib TLS + test
cc -std=c11 -Wall -Wextra -Werror \
  -I vendor/bearssl/inc \
  vendor/bearssl/secureos_compat.c \
  <bearssl_source_files...> \
  user/libs/netlib/ca_bundle.c \
  user/libs/netlib/entropy.c \
  user/libs/netlib/tls.c \
  tests/tls_test.c \
  -o artifacts/tests/tls_test
./artifacts/tests/tls_test
```

**`build/scripts/test_https.sh`**
```bash
# Compile full netlib + BearSSL + HTTPS test
cc -std=c11 -Wall -Wextra -Werror \
  -I vendor/bearssl/inc \
  vendor/bearssl/secureos_compat.c \
  <bearssl_source_files...> \
  user/libs/netlib/*.c (excluding backend_kernel.c) \
  tests/https_test.c \
  -o artifacts/tests/https_test
./artifacts/tests/https_test
```

[Implementation Order]
Implement in dependency order: vendor BearSSL → compat layer → crypto primitives test → TLS engine → HTTPS client → API integration → build system → tests.

1. **Vendor BearSSL source** — Clone BearSSL repository, copy required source files into `vendor/bearssl/`. Create `vendor/bearssl/Makefile.secureos` listing the exact `.c` files needed for TLS 1.2 client with RSA+ECDSA+AES-GCM. Add to `.gitignore`.

2. **Freestanding compat shims** — Create `vendor/bearssl/secureos_compat.c` with `memcpy`, `memmove`, `memset`, `memcmp`, `strlen` implementations. These are the only libc symbols BearSSL requires.

3. **BearSSL build script** — Create `build/scripts/build_bearssl.sh` and `build/scripts/build_bearssl.ps1` that compile all BearSSL `.c` files to `.o` files using `clang --target=i386-unknown-none-elf -ffreestanding -m32 -I vendor/bearssl/inc`.

4. **Entropy module** — Create `user/libs/netlib/entropy.h` and `user/libs/netlib/entropy.c`. Implement `entropy_init()` and `entropy_get_seed()` using RDTSC and a static counter. BearSSL's HMAC-DRBG PRNG requires an initial seed.

5. **CA trust bundle** — Create `user/libs/netlib/ca_bundle.h` and `user/libs/netlib/ca_bundle.c`. Convert 5 root CA certificates from PEM/DER to C byte arrays. Implement `ca_bundle_get()` returning `br_x509_trust_anchor` array.

6. **TLS client module** — Create `user/libs/netlib/tls.h` and `user/libs/netlib/tls.c`. Implement `tls_connect()`, `tls_send()`, `tls_recv()`, `tls_close()` wrapping BearSSL's client engine over the existing TCP layer. Implement the BearSSL I/O callbacks (`tls_sock_read`, `tls_sock_write`) that bridge to `tcp_send`/`tcp_recv`.

7. **HTTPS client module** — Create `user/libs/netlib/https.h` and `user/libs/netlib/https.c`. Implement `https_request()` following the same pattern as `http_request()` but using `tls_connect`/`tls_send`/`tls_recv`/`tls_close` instead of `tcp_connect`/`tcp_send`/`tcp_recv`/`tcp_close`.

8. **Update HTTP URL parser** — Modify `http_parse_url()` in `user/libs/netlib/http.c` to recognize `https://`, set `is_https = 1` and default port 443 instead of rejecting. Update `http_request()` to delegate to `https_request()` when `is_https` is set.

9. **Update netlib API** — Add `netlib_https_get()` to `user/libs/netlib/api.c`. Update `user/include/lib/netlib.h` with the new prototype. Since `netlib_http_get()` calls `http_request()` which now auto-detects HTTPS, existing callers transparently gain HTTPS support.

10. **Update OS ABI** — Add `os_net_https_get()` to `user/include/secureos_api.h` and stub in `user/runtime/secureos_api_stubs.c`. Add `native_net_https_get()` to `kernel/user/native_net_service.h` and implement in `kernel/user/native_net_service.c`. Wire the new syscall in `kernel/user/process.c`.

11. **Update http app** — Update `user/apps/os/http/main.c` help text and `user/os_commands/http.cmd` to document HTTPS support. The app itself needs no code changes since `netlib_http_get()` transparently handles HTTPS.

12. **Update kernel build scripts** — Modify `build/scripts/build_kernel_entry.sh` and `.ps1` to compile BearSSL objects, TLS/HTTPS netlib sources, and link them into the kernel image.

13. **Update user lib build scripts** — Modify `build/scripts/build_user_lib.sh` and `.ps1` to include BearSSL and TLS/HTTPS sources when building `netlib.lib`.

14. **TLS unit tests** — Create `tests/tls_test.c` and `build/scripts/test_tls.sh`. Test BearSSL initialization, trust anchor loading, URL parsing changes, compat shims.

15. **HTTPS unit tests** — Create `tests/https_test.c` and `build/scripts/test_https.sh`. Test HTTPS request formatting, transparent HTTP/HTTPS routing, error handling.

16. **Update test runners** — Add `tls` and `https` targets to `build/scripts/test.sh` and `build/scripts/test.ps1`.

17. **Documentation** — Copy plan to `docs/plans/2026-03-17-netlib-https-bearssl.md`. Update `docs/architecture/CAPABILITIES.md` to note HTTPS coverage under `CAP_NETWORK`.