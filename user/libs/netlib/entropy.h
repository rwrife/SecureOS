#ifndef SECUREOS_NET_ENTROPY_H
#define SECUREOS_NET_ENTROPY_H

/**
 * @file entropy.h
 * @brief Entropy/random seed interface for TLS PRNG initialization.
 *
 * Purpose:
 *   Provides a minimal entropy source for seeding BearSSL's HMAC-DRBG
 *   PRNG.  Combines RDTSC (x86), a monotonic counter, and a static
 *   boot-time seed to produce initial random bytes.
 *
 * Interactions:
 *   - tls.c calls entropy_get_seed() to initialize the BearSSL PRNG
 *     before each TLS handshake.
 *
 * Launched by:
 *   Called on-demand from tls.c.  Not standalone.
 */

#include <stddef.h>
#include <stdint.h>

/**
 * Initialize the entropy pool.  Should be called once early in boot or
 * before the first TLS connection.
 */
void entropy_init(void);

/**
 * Fill out with len pseudo-random bytes suitable for PRNG seeding.
 * The quality is sufficient for TLS client nonces but not for
 * cryptographic key generation.
 */
void entropy_get_seed(uint8_t *out, size_t len);

#endif