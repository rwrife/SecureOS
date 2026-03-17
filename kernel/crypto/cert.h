/**
 * @file cert.h
 * @brief SecureOS lightweight certificate chain types and validation API.
 *
 * Purpose:
 *   Defines the SecureOS certificate format (secureos_cert_t) and
 *   provides functions for parsing, verifying, and validating
 *   certificate chains against a baked-in root Ed25519 public key.
 *
 * Interactions:
 *   - kernel/crypto/ed25519.c provides signature verification.
 *   - kernel/crypto/cert.c implements the functions declared here.
 *   - kernel/format/sof.c uses cert_chain_validate() during signature
 *     verification of SOF binaries.
 *
 * Launched by:
 *   Header-only; not compiled standalone.
 */

#ifndef SECUREOS_CERT_H
#define SECUREOS_CERT_H

#include <stddef.h>
#include <stdint.h>

#include "ed25519.h"

enum {
  SECUREOS_CERT_MAGIC_SIZE = 4,
  SECUREOS_CERT_KEY_HASH_SIZE = 32,
  SECUREOS_CERT_TOTAL_SIZE = 132,
  SECUREOS_CERT_MAX_CHAIN_DEPTH = 4,
};

/**
 * SecureOS certificate: 132 bytes.
 *   [0..3]    magic "SCRT"
 *   [4..35]   issuer_key_hash (SHA-512/256 of issuer public key)
 *   [36..67]  subject_public_key (Ed25519 public key)
 *   [68..131] signature (Ed25519 sig by issuer over bytes [0..67])
 */
typedef struct {
  uint8_t magic[SECUREOS_CERT_MAGIC_SIZE];
  uint8_t issuer_key_hash[SECUREOS_CERT_KEY_HASH_SIZE];
  uint8_t subject_public_key[ED25519_PUBLIC_KEY_SIZE];
  uint8_t signature[ED25519_SIGNATURE_SIZE];
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

/**
 * Parse raw bytes into a secureos_cert_t.
 */
cert_result_t cert_parse(const uint8_t *data, size_t data_len,
                         secureos_cert_t *out);

/**
 * Verify the certificate's signature using the given issuer public key.
 */
cert_result_t cert_verify(const secureos_cert_t *cert,
                          const uint8_t issuer_public_key[ED25519_PUBLIC_KEY_SIZE]);

/**
 * Validate that the certificate's issuer_key_hash matches the hash of
 * root_public_key, and that the certificate signature is valid.
 */
cert_result_t cert_chain_validate(const secureos_cert_t *cert,
                                  const uint8_t root_public_key[ED25519_PUBLIC_KEY_SIZE]);

/**
 * Build a certificate: sign (magic + issuer_key_hash + subject_public_key)
 * with the issuer's private key.  Used by host-side tools.
 */
void cert_build(const uint8_t issuer_public_key[ED25519_PUBLIC_KEY_SIZE],
                const uint8_t issuer_private_key[ED25519_PRIVATE_KEY_SIZE],
                const uint8_t subject_public_key[ED25519_PUBLIC_KEY_SIZE],
                secureos_cert_t *out);

/**
 * Serialize a certificate to raw bytes.
 */
void cert_serialize(const secureos_cert_t *cert, uint8_t out[SECUREOS_CERT_TOTAL_SIZE]);

/**
 * Cache the root public key for verification.
 * Called once during boot (from fs_service_init) to ensure the
 * same derived key is used for both signing and verification.
 */
void cert_set_root_public_key(const uint8_t key[ED25519_PUBLIC_KEY_SIZE]);

/**
 * Return a pointer to the root Ed25519 public key (32 bytes).
 * Returns the cached key if set, otherwise falls back to the baked-in constant.
 */
const uint8_t *cert_get_root_public_key(void);

#endif /* SECUREOS_CERT_H */