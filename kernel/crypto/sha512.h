/**
 * @file sha512.h
 * @brief Freestanding SHA-512 hash function for SecureOS.
 *
 * Purpose:
 *   Provides a SHA-512 implementation per FIPS 180-4 that operates
 *   without libc.  Used by the Ed25519 signature verification module
 *   and certificate chain validation.
 *
 * Interactions:
 *   - kernel/crypto/ed25519.c uses sha512_hash() for Ed25519 internals.
 *   - kernel/crypto/cert.c uses sha512_hash() for public key hashing.
 *   - kernel/format/sof.c uses sha512_hash() for payload hashing.
 *
 * Launched by:
 *   Header-only; not compiled standalone.
 */

#ifndef SECUREOS_SHA512_H
#define SECUREOS_SHA512_H

#include <stddef.h>
#include <stdint.h>

enum {
  SHA512_HASH_SIZE = 64,
  SHA512_BLOCK_SIZE = 128,
};

typedef struct {
  uint64_t state[8];
  uint64_t count[2];
  uint8_t  buffer[SHA512_BLOCK_SIZE];
} sha512_ctx_t;

/**
 * Initialize a SHA-512 context with the standard IV.
 */
void sha512_init(sha512_ctx_t *ctx);

/**
 * Feed data into the SHA-512 context.
 */
void sha512_update(sha512_ctx_t *ctx, const uint8_t *data, size_t len);

/**
 * Finalize the hash and write 64 bytes to out.
 */
void sha512_final(sha512_ctx_t *ctx, uint8_t out[SHA512_HASH_SIZE]);

/**
 * One-shot convenience: hash data and write 64 bytes to out.
 */
void sha512_hash(const uint8_t *data, size_t len, uint8_t out[SHA512_HASH_SIZE]);

#endif /* SECUREOS_SHA512_H */