/**
 * @file user/apps/cc/cc_sha256.c
 * @brief Freestanding SHA-256 implementation for the in-OS `cc` driver.
 *
 * Purpose:
 *   Computes the `cc.compile.success` SOF digest (docs/abi/audit-markers.md
 *   §3.1) inside the app runtime, where the kernel's sha512 code is not
 *   linkable. Standard FIPS 180-4 SHA-256 message schedule + compression,
 *   no dynamic allocation beyond the caller-provided output.
 *
 * Interactions:
 *   - cc_sha256.h — public contract.
 *   - user/apps/cc/main.c — success marker emission.
 *   - tests/m7_toolchain/cc_sha256_test.c — host NIST-vector gate.
 *
 * Launched by: linked into the cc app image only (app-local, not clib ABI).
 */

#include "cc_sha256.h"

static const uint32_t sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t rotr32(uint32_t x, unsigned n) {
  return (x >> n) | (x << (32u - n));
}

static void sha256_compress(uint32_t h[8], const unsigned char block[64]) {
  uint32_t w[64];
  uint32_t a, b, c, d, e, f, g, hh;
  unsigned i;

  for (i = 0; i < 16u; ++i) {
    w[i] = ((uint32_t)block[i * 4u] << 24) | ((uint32_t)block[i * 4u + 1u] << 16)
           | ((uint32_t)block[i * 4u + 2u] << 8) | (uint32_t)block[i * 4u + 3u];
  }
  for (; i < 64u; ++i) {
    uint32_t s0 = rotr32(w[i - 15u], 7u) ^ rotr32(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
    uint32_t s1 = rotr32(w[i - 2u], 17u) ^ rotr32(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
    w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
  }

  a = h[0]; b = h[1]; c = h[2]; d = h[3];
  e = h[4]; f = h[5]; g = h[6]; hh = h[7];

  for (i = 0; i < 64u; ++i) {
    uint32_t s1 = rotr32(e, 6u) ^ rotr32(e, 11u) ^ rotr32(e, 25u);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t temp1 = hh + s1 + ch + sha256_k[i] + w[i];
    uint32_t s0 = rotr32(a, 2u) ^ rotr32(a, 13u) ^ rotr32(a, 22u);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = s0 + maj;

    hh = g; g = f; f = e; e = d + temp1;
    d = c; c = b; b = a; a = temp1 + temp2;
  }

  h[0] += a; h[1] += b; h[2] += c; h[3] += d;
  h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static const char k_hex[] = "0123456789abcdef";

int cc_sha256_hex(const void *data, size_t len, char *out_hex) {
  static const size_t CHUNK = 64;
  uint32_t h[8];
  unsigned char block[CHUNK];
  uint64_t total_bits;
  size_t offset = 0;
  const unsigned char *bytes = (const unsigned char *)data;
  unsigned i;

  if (data == 0 || out_hex == 0) {
    return -1;
  }

  h[0] = 0x6a09e667u; h[1] = 0xbb67ae85u; h[2] = 0x3c6ef372u; h[3] = 0xa54ff53au;
  h[4] = 0x510e527fu; h[5] = 0x9b05688cu; h[6] = 0x1f83d9abu; h[7] = 0x5be0cd19u;

  total_bits = (uint64_t)len * 8u;

  while (offset + CHUNK <= len) {
    sha256_compress(h, bytes + offset);
    offset += CHUNK;
  }

  /* Tail: copy remaining bytes, append 0x80, zero-pad, write 64-bit BE
   * bit-length into the final 8 bytes (single padding block suffices for
   * any tail < 56 bytes; longer tails get one extra block). */
  {
    size_t remaining = len - offset;
    for (i = 0; i < (unsigned)remaining; ++i) {
      block[i] = bytes[offset + i];
    }
    block[remaining] = 0x80u;
    if (remaining >= 56u) {
      for (i = (unsigned)remaining + 1u; i < CHUNK; ++i) {
        block[i] = 0u;
      }
      sha256_compress(h, block);
      remaining = 0u;
      for (i = 0; i < 56u; ++i) {
        block[i] = 0u;
      }
    } else {
      for (i = (unsigned)remaining + 1u; i < 56u; ++i) {
        block[i] = 0u;
      }
    }
    for (i = 0; i < 8u; ++i) {
      block[56u + i] = (unsigned char)(total_bits >> (56u - 8u * i));
    }
    sha256_compress(h, block);
  }

  for (i = 0; i < 8u; ++i) {
    out_hex[i * 8u + 0u] = k_hex[(h[i] >> 28) & 0xFu];
    out_hex[i * 8u + 1u] = k_hex[(h[i] >> 24) & 0xFu];
    out_hex[i * 8u + 2u] = k_hex[(h[i] >> 20) & 0xFu];
    out_hex[i * 8u + 3u] = k_hex[(h[i] >> 16) & 0xFu];
    out_hex[i * 8u + 4u] = k_hex[(h[i] >> 12) & 0xFu];
    out_hex[i * 8u + 5u] = k_hex[(h[i] >> 8) & 0xFu];
    out_hex[i * 8u + 6u] = k_hex[(h[i] >> 4) & 0xFu];
    out_hex[i * 8u + 7u] = k_hex[h[i] & 0xFu];
  }
  out_hex[CC_SHA256_HEX_CHARS] = '\0';
  return 0;
}
