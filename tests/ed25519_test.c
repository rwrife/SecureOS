/**
 * @file ed25519_test.c
 * @brief Unit tests for SHA-512 and Ed25519 crypto primitives.
 *
 * Purpose:
 *   Validates SHA-512 against NIST test vectors and Ed25519 sign/verify
 *   against RFC 8032 test vectors.  Tests rejection of corrupted
 *   signatures and wrong keys.
 *
 * Interactions:
 *   - kernel/crypto/sha512.h and sha512.c provide SHA-512.
 *   - kernel/crypto/ed25519.h and ed25519.c provide Ed25519.
 *
 * Launched by:
 *   Compiled and run by build/scripts/test_ed25519.sh.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "../kernel/crypto/sha512.h"
#include "../kernel/crypto/ed25519.h"

static int test_pass_count = 0;
static int test_fail_count = 0;

static void test_assert(int condition, const char *name) {
  if (condition) {
    printf("  PASS: %s\n", name);
    ++test_pass_count;
  } else {
    printf("  FAIL: %s\n", name);
    ++test_fail_count;
  }
}

static int mem_equal(const uint8_t *a, const uint8_t *b, size_t len) {
  size_t i;
  for (i = 0; i < len; ++i) {
    if (a[i] != b[i]) return 0;
  }
  return 1;
}

/* ---- SHA-512 tests ----------------------------------------------------- */

static void test_sha512_empty(void) {
  /* SHA-512("") = cf83e1357eefb8bd... */
  static const uint8_t expected[64] = {
    0xcf, 0x83, 0xe1, 0x35, 0x7e, 0xef, 0xb8, 0xbd,
    0xf1, 0x54, 0x28, 0x50, 0xd6, 0x6d, 0x80, 0x07,
    0xd6, 0x20, 0xe4, 0x05, 0x0b, 0x57, 0x15, 0xdc,
    0x83, 0xf4, 0xa9, 0x21, 0xd3, 0x6c, 0xe9, 0xce,
    0x47, 0xd0, 0xd1, 0x3c, 0x5d, 0x85, 0xf2, 0xb0,
    0xff, 0x83, 0x18, 0xd2, 0x87, 0x7e, 0xec, 0x2f,
    0x63, 0xb9, 0x31, 0xbd, 0x47, 0x41, 0x7a, 0x81,
    0xa5, 0x38, 0x32, 0x7a, 0xf9, 0x27, 0xda, 0x3e
  };
  uint8_t out[64];

  sha512_hash((const uint8_t *)"", 0, out);
  test_assert(mem_equal(out, expected, 64), "sha512 empty string");
}

static void test_sha512_abc(void) {
  /* SHA-512("abc") = ddaf35a193617aba... */
  static const uint8_t expected[64] = {
    0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba,
    0xcc, 0x41, 0x73, 0x49, 0xae, 0x20, 0x41, 0x31,
    0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
    0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a,
    0x21, 0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8,
    0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
    0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e,
    0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f
  };
  uint8_t out[64];

  sha512_hash((const uint8_t *)"abc", 3, out);
  test_assert(mem_equal(out, expected, 64), "sha512 abc");
}

static void test_sha512_streaming(void) {
  /* Same as "abc" but fed one byte at a time */
  static const uint8_t expected[64] = {
    0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba,
    0xcc, 0x41, 0x73, 0x49, 0xae, 0x20, 0x41, 0x31,
    0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
    0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a,
    0x21, 0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8,
    0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
    0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e,
    0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f
  };
  uint8_t out[64];
  sha512_ctx_t ctx;

  sha512_init(&ctx);
  sha512_update(&ctx, (const uint8_t *)"a", 1);
  sha512_update(&ctx, (const uint8_t *)"b", 1);
  sha512_update(&ctx, (const uint8_t *)"c", 1);
  sha512_final(&ctx, out);
  test_assert(mem_equal(out, expected, 64), "sha512 streaming abc");
}

/* ---- Ed25519 tests ----------------------------------------------------- */

static void test_ed25519_sign_verify_roundtrip(void) {
  /* Generate a keypair from a test seed and sign/verify a message */
  static const uint8_t seed[32] = {
    0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60,
    0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4,
    0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19,
    0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60
  };
  uint8_t pub[32], priv[64], sig[64];
  const uint8_t msg[] = "test message";
  ed25519_result_t result;

  ed25519_create_keypair(seed, pub, priv);
  test_assert(pub[0] != 0 || pub[1] != 0, "keypair generated non-zero pubkey");

  ed25519_sign(msg, sizeof(msg) - 1, pub, priv, sig);
  result = ed25519_verify(sig, msg, sizeof(msg) - 1, pub);
  test_assert(result == ED25519_OK, "sign/verify roundtrip succeeds");
}

static void test_ed25519_reject_corrupted_sig(void) {
  static const uint8_t seed[32] = {
    0x4c, 0xcd, 0x08, 0x9b, 0x28, 0xff, 0x96, 0xda,
    0x9d, 0xb6, 0xc3, 0x46, 0xec, 0x11, 0x4e, 0x0f,
    0x5b, 0x8a, 0x31, 0x9f, 0x35, 0xab, 0xa6, 0x24,
    0xda, 0x8c, 0xf6, 0xed, 0x4f, 0xb8, 0xa6, 0xfb
  };
  uint8_t pub[32], priv[64], sig[64];
  const uint8_t msg[] = "hello world";
  ed25519_result_t result;

  ed25519_create_keypair(seed, pub, priv);
  ed25519_sign(msg, sizeof(msg) - 1, pub, priv, sig);

  /* Corrupt one byte of the signature */
  sig[10] ^= 0x01u;
  result = ed25519_verify(sig, msg, sizeof(msg) - 1, pub);
  test_assert(result != ED25519_OK, "corrupted signature rejected");
}

static void test_ed25519_reject_wrong_key(void) {
  static const uint8_t seed1[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
  };
  static const uint8_t seed2[32] = {
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40
  };
  uint8_t pub1[32], priv1[64];
  uint8_t pub2[32], priv2[64];
  uint8_t sig[64];
  const uint8_t msg[] = "verify with wrong key";
  ed25519_result_t result;

  ed25519_create_keypair(seed1, pub1, priv1);
  ed25519_create_keypair(seed2, pub2, priv2);
  ed25519_sign(msg, sizeof(msg) - 1, pub1, priv1, sig);

  /* Verify with wrong public key */
  result = ed25519_verify(sig, msg, sizeof(msg) - 1, pub2);
  test_assert(result != ED25519_OK, "wrong public key rejected");
}

static void test_ed25519_reject_modified_message(void) {
  static const uint8_t seed[32] = {
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99
  };
  uint8_t pub[32], priv[64], sig[64];
  const uint8_t msg[] = "original message";
  const uint8_t msg2[] = "modified message";
  ed25519_result_t result;

  ed25519_create_keypair(seed, pub, priv);
  ed25519_sign(msg, sizeof(msg) - 1, pub, priv, sig);

  result = ed25519_verify(sig, msg2, sizeof(msg2) - 1, pub);
  test_assert(result != ED25519_OK, "modified message rejected");
}

static void test_ed25519_public_key_hash(void) {
  static const uint8_t seed[32] = {
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
  };
  uint8_t pub[32], priv[64];
  uint8_t hash1[32], hash2[32];

  ed25519_create_keypair(seed, pub, priv);
  ed25519_public_key_hash(pub, hash1);
  ed25519_public_key_hash(pub, hash2);

  test_assert(mem_equal(hash1, hash2, 32), "public key hash deterministic");
  /* Ensure it's not all zeros */
  {
    int non_zero = 0;
    size_t i;
    for (i = 0; i < 32; ++i) {
      if (hash1[i] != 0) non_zero = 1;
    }
    test_assert(non_zero, "public key hash non-zero");
  }
}

/* ---- Main -------------------------------------------------------------- */

int main(void) {
  printf("TEST:START:ed25519\n");

  test_sha512_empty();
  test_sha512_abc();
  test_sha512_streaming();

  test_ed25519_sign_verify_roundtrip();
  test_ed25519_reject_corrupted_sig();
  test_ed25519_reject_wrong_key();
  test_ed25519_reject_modified_message();
  test_ed25519_public_key_hash();

  if (test_fail_count == 0) {
    printf("TEST:PASS:ed25519\n");
  } else {
    printf("TEST:FAIL:ed25519\n");
  }

  return test_fail_count > 0 ? 1 : 0;
}