/**
 * @file cert.c
 * @brief SecureOS certificate parsing, building, and chain validation.
 *
 * Purpose:
 *   Implements the lightweight SecureOS certificate format operations:
 *   parsing raw bytes, verifying signatures against issuer keys,
 *   validating chains to the baked-in root key, and building certs.
 *
 * Interactions:
 *   - cert.h declares all types and function prototypes.
 *   - ed25519.c provides signature verification and signing.
 *   - sha512.c provides hashing for issuer key matching.
 *   - root_key.h provides the baked-in root public key.
 *   - sof.c calls cert_chain_validate() during signature verification.
 *
 * Launched by:
 *   Compiled into the kernel image and host tools.
 */

#include "cert.h"
#include "sha512.h"
#include "root_key.h"

static const uint8_t CERT_MAGIC[4] = {0x53, 0x43, 0x52, 0x54}; /* "SCRT" */

static int cert_mem_equal(const uint8_t *a, const uint8_t *b, size_t len) {
  size_t i;
  uint8_t diff = 0u;
  for (i = 0; i < len; ++i) {
    diff |= a[i] ^ b[i];
  }
  return diff == 0u;
}

static void cert_memcpy(uint8_t *dst, const uint8_t *src, size_t len) {
  size_t i;
  for (i = 0; i < len; ++i) {
    dst[i] = src[i];
  }
}

cert_result_t cert_parse(const uint8_t *data, size_t data_len,
                         secureos_cert_t *out) {
  if (data == 0 || out == 0) {
    return CERT_ERR_INVALID_FORMAT;
  }

  if (data_len < SECUREOS_CERT_TOTAL_SIZE) {
    return CERT_ERR_INVALID_FORMAT;
  }

  /* Check magic */
  if (data[0] != CERT_MAGIC[0] || data[1] != CERT_MAGIC[1] ||
      data[2] != CERT_MAGIC[2] || data[3] != CERT_MAGIC[3]) {
    return CERT_ERR_INVALID_MAGIC;
  }

  cert_memcpy(out->magic, data, 4u);
  cert_memcpy(out->issuer_key_hash, data + 4u, SECUREOS_CERT_KEY_HASH_SIZE);
  cert_memcpy(out->subject_public_key, data + 4u + SECUREOS_CERT_KEY_HASH_SIZE,
              ED25519_PUBLIC_KEY_SIZE);
  cert_memcpy(out->signature,
              data + 4u + SECUREOS_CERT_KEY_HASH_SIZE + ED25519_PUBLIC_KEY_SIZE,
              ED25519_SIGNATURE_SIZE);

  return CERT_OK;
}

cert_result_t cert_verify(const secureos_cert_t *cert,
                          const uint8_t issuer_public_key[ED25519_PUBLIC_KEY_SIZE]) {
  /* The signed data is: magic(4) + issuer_key_hash(32) + subject_public_key(32) = 68 bytes */
  uint8_t signed_data[4 + SECUREOS_CERT_KEY_HASH_SIZE + ED25519_PUBLIC_KEY_SIZE];
  ed25519_result_t result;

  if (cert == 0 || issuer_public_key == 0) {
    return CERT_ERR_INVALID_FORMAT;
  }

  cert_memcpy(signed_data, cert->magic, 4u);
  cert_memcpy(signed_data + 4u, cert->issuer_key_hash, SECUREOS_CERT_KEY_HASH_SIZE);
  cert_memcpy(signed_data + 4u + SECUREOS_CERT_KEY_HASH_SIZE,
              cert->subject_public_key, ED25519_PUBLIC_KEY_SIZE);

  result = ed25519_verify(cert->signature, signed_data, sizeof(signed_data),
                          issuer_public_key);

  if (result != ED25519_OK) {
    return CERT_ERR_SIGNATURE_INVALID;
  }

  return CERT_OK;
}

cert_result_t cert_chain_validate(const secureos_cert_t *cert,
                                  const uint8_t root_public_key[ED25519_PUBLIC_KEY_SIZE]) {
  uint8_t root_hash[32];

  if (cert == 0 || root_public_key == 0) {
    return CERT_ERR_INVALID_FORMAT;
  }

  /* Verify the cert magic */
  if (cert->magic[0] != CERT_MAGIC[0] || cert->magic[1] != CERT_MAGIC[1] ||
      cert->magic[2] != CERT_MAGIC[2] || cert->magic[3] != CERT_MAGIC[3]) {
    return CERT_ERR_INVALID_MAGIC;
  }

  /* Check that issuer_key_hash matches the root key hash */
  ed25519_public_key_hash(root_public_key, root_hash);
  if (!cert_mem_equal(cert->issuer_key_hash, root_hash, 32u)) {
    return CERT_ERR_ISSUER_MISMATCH;
  }

  /* Verify the certificate signature using the root key */
  return cert_verify(cert, root_public_key);
}

void cert_build(const uint8_t issuer_public_key[ED25519_PUBLIC_KEY_SIZE],
                const uint8_t issuer_private_key[ED25519_PRIVATE_KEY_SIZE],
                const uint8_t subject_public_key[ED25519_PUBLIC_KEY_SIZE],
                secureos_cert_t *out) {
  uint8_t signed_data[4 + SECUREOS_CERT_KEY_HASH_SIZE + ED25519_PUBLIC_KEY_SIZE];

  if (out == 0) {
    return;
  }

  /* Set magic */
  out->magic[0] = CERT_MAGIC[0];
  out->magic[1] = CERT_MAGIC[1];
  out->magic[2] = CERT_MAGIC[2];
  out->magic[3] = CERT_MAGIC[3];

  /* Compute issuer key hash */
  ed25519_public_key_hash(issuer_public_key, out->issuer_key_hash);

  /* Copy subject public key */
  cert_memcpy(out->subject_public_key, subject_public_key, ED25519_PUBLIC_KEY_SIZE);

  /* Build the data to sign */
  cert_memcpy(signed_data, out->magic, 4u);
  cert_memcpy(signed_data + 4u, out->issuer_key_hash, SECUREOS_CERT_KEY_HASH_SIZE);
  cert_memcpy(signed_data + 4u + SECUREOS_CERT_KEY_HASH_SIZE,
              out->subject_public_key, ED25519_PUBLIC_KEY_SIZE);

  /* Sign with issuer's key */
  ed25519_sign(signed_data, sizeof(signed_data),
               issuer_public_key, issuer_private_key, out->signature);
}

void cert_serialize(const secureos_cert_t *cert, uint8_t out[SECUREOS_CERT_TOTAL_SIZE]) {
  if (cert == 0 || out == 0) {
    return;
  }

  cert_memcpy(out, cert->magic, 4u);
  cert_memcpy(out + 4u, cert->issuer_key_hash, SECUREOS_CERT_KEY_HASH_SIZE);
  cert_memcpy(out + 4u + SECUREOS_CERT_KEY_HASH_SIZE,
              cert->subject_public_key, ED25519_PUBLIC_KEY_SIZE);
  cert_memcpy(out + 4u + SECUREOS_CERT_KEY_HASH_SIZE + ED25519_PUBLIC_KEY_SIZE,
              cert->signature, ED25519_SIGNATURE_SIZE);
}

static uint8_t cert_root_key_cache[ED25519_PUBLIC_KEY_SIZE];
static int cert_root_key_set = 0;

void cert_set_root_public_key(const uint8_t key[ED25519_PUBLIC_KEY_SIZE]) {
  size_t i;
  for (i = 0; i < ED25519_PUBLIC_KEY_SIZE; ++i) {
    cert_root_key_cache[i] = key[i];
  }
  cert_root_key_set = 1;
}

const uint8_t *cert_get_root_public_key(void) {
  if (cert_root_key_set) {
    return cert_root_key_cache;
  }
  /* Fallback to baked-in key (may mismatch on 32-bit) */
  return SECUREOS_ROOT_PUBLIC_KEY;
}
