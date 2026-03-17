/**
 * @file cert_chain_test.c
 * @brief Unit tests for SecureOS certificate chain validation.
 *
 * Launched by: build/scripts/test_cert_chain.sh
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "../kernel/crypto/sha512.h"
#include "../kernel/crypto/ed25519.h"
#include "../kernel/crypto/cert.h"
#include "../kernel/crypto/root_key.h"

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

static void test_cert_build_and_parse(void) {
  uint8_t root_pub[32], root_priv[64];
  uint8_t inter_pub[32], inter_priv[64];
  secureos_cert_t cert;
  uint8_t raw[SECUREOS_CERT_TOTAL_SIZE];
  secureos_cert_t parsed;

  ed25519_create_keypair(SECUREOS_ROOT_SEED, root_pub, root_priv);
  ed25519_create_keypair(SECUREOS_INTERMEDIATE_SEED, inter_pub, inter_priv);

  cert_build(root_pub, root_priv, inter_pub, &cert);
  test_assert(cert.magic[0] == 0x53 && cert.magic[1] == 0x43, "cert magic set");

  cert_serialize(&cert, raw);
  test_assert(cert_parse(raw, sizeof(raw), &parsed) == CERT_OK, "cert parse succeeds");
}

static void test_cert_verify_valid(void) {
  uint8_t root_pub[32], root_priv[64];
  uint8_t inter_pub[32], inter_priv[64];
  secureos_cert_t cert;

  ed25519_create_keypair(SECUREOS_ROOT_SEED, root_pub, root_priv);
  ed25519_create_keypair(SECUREOS_INTERMEDIATE_SEED, inter_pub, inter_priv);

  cert_build(root_pub, root_priv, inter_pub, &cert);
  test_assert(cert_verify(&cert, root_pub) == CERT_OK, "cert verify valid");
}

static void test_cert_verify_wrong_key(void) {
  uint8_t root_pub[32], root_priv[64];
  uint8_t inter_pub[32], inter_priv[64];
  uint8_t other_pub[32], other_priv[64];
  secureos_cert_t cert;
  static const uint8_t other_seed[32] = {
    0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8,
    0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0xF1, 0xF0,
    0xEF, 0xEE, 0xED, 0xEC, 0xEB, 0xEA, 0xE9, 0xE8,
    0xE7, 0xE6, 0xE5, 0xE4, 0xE3, 0xE2, 0xE1, 0xE0
  };

  ed25519_create_keypair(SECUREOS_ROOT_SEED, root_pub, root_priv);
  ed25519_create_keypair(SECUREOS_INTERMEDIATE_SEED, inter_pub, inter_priv);
  ed25519_create_keypair(other_seed, other_pub, other_priv);

  cert_build(root_pub, root_priv, inter_pub, &cert);
  test_assert(cert_verify(&cert, other_pub) != CERT_OK, "cert verify wrong issuer key rejected");
}

static void test_cert_chain_validate_valid(void) {
  uint8_t root_pub[32], root_priv[64];
  uint8_t inter_pub[32], inter_priv[64];
  secureos_cert_t cert;

  ed25519_create_keypair(SECUREOS_ROOT_SEED, root_pub, root_priv);
  ed25519_create_keypair(SECUREOS_INTERMEDIATE_SEED, inter_pub, inter_priv);

  cert_build(root_pub, root_priv, inter_pub, &cert);
  test_assert(cert_chain_validate(&cert, root_pub) == CERT_OK,
              "cert chain validates to root");
}

static void test_cert_chain_validate_wrong_root(void) {
  uint8_t root_pub[32], root_priv[64];
  uint8_t inter_pub[32], inter_priv[64];
  uint8_t fake_root_pub[32], fake_root_priv[64];
  secureos_cert_t cert;
  static const uint8_t fake_seed[32] = {
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99
  };

  ed25519_create_keypair(SECUREOS_ROOT_SEED, root_pub, root_priv);
  ed25519_create_keypair(SECUREOS_INTERMEDIATE_SEED, inter_pub, inter_priv);
  ed25519_create_keypair(fake_seed, fake_root_pub, fake_root_priv);

  /* Cert built with real root, but validated against fake root */
  cert_build(root_pub, root_priv, inter_pub, &cert);
  test_assert(cert_chain_validate(&cert, fake_root_pub) != CERT_OK,
              "cert chain rejects wrong root");
}

static void test_cert_parse_bad_magic(void) {
  uint8_t raw[SECUREOS_CERT_TOTAL_SIZE] = {0};
  secureos_cert_t parsed;

  raw[0] = 0xFF;
  test_assert(cert_parse(raw, sizeof(raw), &parsed) == CERT_ERR_INVALID_MAGIC,
              "cert parse rejects bad magic");
}

static void test_cert_parse_truncated(void) {
  uint8_t raw[64] = {0x53, 0x43, 0x52, 0x54};
  secureos_cert_t parsed;

  test_assert(cert_parse(raw, sizeof(raw), &parsed) == CERT_ERR_INVALID_FORMAT,
              "cert parse rejects truncated data");
}

static void test_cert_get_root_key(void) {
  const uint8_t *key = cert_get_root_public_key();
  int non_zero = 0;
  size_t i;

  test_assert(key != 0, "root key accessible");
  for (i = 0; i < 32; ++i) {
    if (key[i] != 0) non_zero = 1;
  }
  test_assert(non_zero, "root key non-zero");
}

int main(void) {
  printf("TEST:START:cert_chain\n");

  test_cert_build_and_parse();
  test_cert_verify_valid();
  test_cert_verify_wrong_key();
  test_cert_chain_validate_valid();
  test_cert_chain_validate_wrong_root();
  test_cert_parse_bad_magic();
  test_cert_parse_truncated();
  test_cert_get_root_key();

  if (test_fail_count == 0) {
    printf("TEST:PASS:cert_chain\n");
  } else {
    printf("TEST:FAIL:cert_chain\n");
  }

  return test_fail_count > 0 ? 1 : 0;
}