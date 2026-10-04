/**
 * @file user/apps/cc/cc_sha256.h
 * @brief Freestanding SHA-256 for the in-OS `cc` driver's compile markers.
 *
 * Purpose:
 *   `cc.compile.success` (docs/abi/audit-markers.md §3.1) requires a
 *   lowercase 64-hex SHA-256 of the emitted SOF bytes, and `manifest.synth.ok`
 *   (§3.2) consumes the first 12 hex chars of the same digest. The kernel's
 *   sha512 implementation lives behind kernel headers the freestanding app
 *   surface cannot include, so the driver carries this small, dependency-free
 *   implementation instead.
 *
 * Interactions:
 *   - user/apps/cc/main.c — success-marker emission.
 *   - tests/m7_toolchain/cc_sha256_test.c — host vectors (NIST suite).
 *   - build/scripts/test_cc_sha256.sh — host gate.
 *
 * Containment: app-local helper (not clib ABI); no public symbol drift gates
 * apply. Freestanding: <stdint.h>/<stddef.h> only.
 */

#ifndef SECUREOS_USER_APPS_CC_SHA256_H
#define SECUREOS_USER_APPS_CC_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of lowercase hex characters cc_sha256_hex writes (plus NUL). */
#define CC_SHA256_HEX_CHARS 64

/**
 * Compute the SHA-256 digest of `data[0..len)` and write it as 64 lowercase
 * hex characters plus a NUL terminator into `out_hex` (which must hold 65
 * bytes). Returns 0 on success, -1 on invalid arguments.
 */
int cc_sha256_hex(const void *data, size_t len, char *out_hex);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_APPS_CC_SHA256_H */
