/**
 * @file entropy.c
 * @brief Entropy collection for TLS PRNG seeding.
 *
 * Purpose:
 *   Provides a basic entropy source for the BearSSL HMAC-DRBG PRNG.
 *   On x86 targets, reads the Time Stamp Counter (RDTSC) for jitter.
 *   A monotonic invocation counter and a static compile-time seed are
 *   mixed together to produce pseudo-random output.
 *
 * Interactions:
 *   - entropy.h declares the public API.
 *   - tls.c calls entropy_get_seed() before each TLS handshake.
 *
 * Launched by:
 *   Called on-demand from tls.c.  Not standalone.
 */

#include "entropy.h"

/* Static state for the entropy pool */
static uint32_t g_entropy_counter = 0u;
static uint32_t g_entropy_state[4] = {
  0xDEADBEEFu, 0xCAFEBABEu, 0x8BADF00Du, 0xFEEDFACEu
};
static int g_entropy_initialized = 0;

/**
 * Read the low 32 bits of the x86 Time Stamp Counter.
 * Falls back to the monotonic counter on non-x86 platforms.
 */
static uint32_t entropy_rdtsc_low(void) {
#if defined(__i386__) || defined(__x86_64__)
  uint32_t lo;
  uint32_t hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return lo;
#else
  /* Non-x86 fallback: just use counter with multiplication */
  g_entropy_counter += 7u;
  return g_entropy_counter * 2654435761u;
#endif
}

/**
 * Simple 32-bit xorshift mixing function.
 */
static uint32_t entropy_mix(uint32_t x) {
  x ^= x << 13u;
  x ^= x >> 17u;
  x ^= x << 5u;
  return x;
}

void entropy_init(void) {
  uint32_t tsc;

  if (g_entropy_initialized) {
    return;
  }

  tsc = entropy_rdtsc_low();
  g_entropy_state[0] ^= tsc;
  g_entropy_state[1] ^= entropy_mix(tsc + 1u);
  g_entropy_state[2] ^= entropy_mix(tsc + 2u);
  g_entropy_state[3] ^= entropy_mix(tsc + 3u);
  g_entropy_counter = tsc;
  g_entropy_initialized = 1;
}

void entropy_get_seed(uint8_t *out, size_t len) {
  size_t i;
  uint32_t word;

  if (out == 0 || len == 0u) {
    return;
  }

  if (!g_entropy_initialized) {
    entropy_init();
  }

  for (i = 0u; i < len; ++i) {
    if ((i & 3u) == 0u) {
      /* Refresh a word of entropy every 4 bytes */
      g_entropy_counter += 1u;
      g_entropy_state[0] = entropy_mix(g_entropy_state[0] ^ entropy_rdtsc_low());
      g_entropy_state[1] = entropy_mix(g_entropy_state[1] ^ g_entropy_counter);
      g_entropy_state[2] = entropy_mix(g_entropy_state[2] + g_entropy_state[0]);
      g_entropy_state[3] = entropy_mix(g_entropy_state[3] + g_entropy_state[1]);
      word = g_entropy_state[0] ^ g_entropy_state[1] ^
             g_entropy_state[2] ^ g_entropy_state[3];
    }
    out[i] = (uint8_t)((word >> ((i & 3u) * 8u)) & 0xFFu);
  }
}