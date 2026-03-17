/**
 * @file ed25519.h
 * @brief Freestanding Ed25519 signature verification for SecureOS.
 *
 * Purpose:
 *   Provides Ed25519 digital signature verification and signing without
 *   libc dependencies.  The kernel uses only ed25519_verify().  Host-side
 *   tools (keygen, sof_wrap) additionally use ed25519_sign() and
 *   ed25519_create_keypair().
 *
 * Interactions:
 *   - kernel/crypto/sha512.c provides the SHA-512 hash used internally.
 *   - kernel/crypto/cert.c uses ed25519_verify() for certificate chain
 *     validation and ed25519_public_key_hash() for issuer matching.
 *   - kernel/format/sof.c uses ed25519_verify() to verify SOF payload
 *     signatures.
 *   - tools/keygen/main.c uses ed25519_create_keypair() and ed25519_sign().
 *   - tools/sof_wrap/main.c uses ed25519_sign() to sign SOF files.
 *
 * Launched by:
 *   Header-only; not compiled standalone.
 */

#ifndef SECUREOS_ED25519_H
#define SECUREOS_ED25519_H

#include <stddef.h>
#include <stdint.h>

enum {
  ED25519_PUBLIC_KEY_SIZE = 32,
  ED25519_PRIVATE_KEY_SIZE = 64,
  ED25519_SEED_SIZE = 32,
  ED25519_SIGNATURE_SIZE = 64,
};

typedef enum {
  ED25519_OK = 0,
  ED25519_ERR_INVALID_KEY = 1,
  ED25519_ERR_INVALID_SIGNATURE = 2,
  ED25519_ERR_VERIFY_FAILED = 3,
} ed25519_result_t;

/**
 * Verify an Ed25519 signature.
 * Returns ED25519_OK if the signature is valid for the given message and
 * public key.
 */
ed25519_result_t ed25519_verify(const uint8_t signature[ED25519_SIGNATURE_SIZE],
                                const uint8_t *message,
                                size_t message_len,
                                const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE]);

/**
 * Compute a 32-byte hash of an Ed25519 public key (SHA-512 truncated to
 * first 32 bytes).  Used for certificate issuer key matching.
 */
void ed25519_public_key_hash(const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                             uint8_t out_hash[32]);

/**
 * Create an Ed25519 keypair from a 32-byte seed.
 * Writes the 32-byte public key and 64-byte expanded private key.
 * Used by host-side tools only (not needed in the kernel).
 */
void ed25519_create_keypair(const uint8_t seed[ED25519_SEED_SIZE],
                            uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                            uint8_t private_key[ED25519_PRIVATE_KEY_SIZE]);

/**
 * Sign a message with an Ed25519 private key.
 * Used by host-side tools only (not needed in the kernel).
 */
void ed25519_sign(const uint8_t *message,
                  size_t message_len,
                  const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                  const uint8_t private_key[ED25519_PRIVATE_KEY_SIZE],
                  uint8_t signature[ED25519_SIGNATURE_SIZE]);

#endif /* SECUREOS_ED25519_H */