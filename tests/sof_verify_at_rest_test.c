/**
 * @file sof_verify_at_rest_test.c
 * @brief Re-sign discipline + verify-at-rest tests (issue #138).
 *
 * Purpose:
 *   Locks down the contract that a SOF binary signed by sof_build_signed,
 *   persisted to disk, re-read into a separate buffer, and parsed again,
 *   verifies successfully via sof_verify_signature against the chain
 *   rooted at SECUREOS_ROOT_PUBLIC_KEY. Companion negative case asserts
 *   that a single-byte mutation of the persisted signature bytes is
 *   rejected by the verifier.
 *
 *   This is the at-rest counterpart to the in-memory roundtrip in
 *   tests/codesign_test.c (#131/PR #132) and the RFC 8032 known-answer
 *   coverage in tests/ed25519_kat_test.c (#137/PR #143). Together the
 *   three lock down: math (KAT) -> in-memory roundtrip -> on-disk
 *   roundtrip.
 *
 *   Specifically asserts the regression risk #138 calls out: that after
 *   the #133 / PR #134 ed25519 fix lands, a signature produced by the
 *   corrected signer and persisted to disk verifies cleanly when re-read,
 *   so no committed artifact ends up trusted-by-broken-symmetry.
 *
 * Interactions:
 *   - kernel/format/sof.c: sof_build_signed / sof_parse / sof_verify_signature
 *   - kernel/crypto/{sha512,ed25519,cert}.c: crypto primitives
 *   - kernel/crypto/root_key.h: trust anchor + intermediate seed
 *
 * Launched by:
 *   build/scripts/test_sof_verify_at_rest.sh compiles and runs this test.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../kernel/crypto/sha512.h"
#include "../kernel/crypto/ed25519.h"
#include "../kernel/crypto/cert.h"
#include "../kernel/crypto/root_key.h"
#include "../kernel/format/sof.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(const char *label, int condition) {
  if (condition) {
    printf("  PASS: %s\n", label);
    ++g_pass;
  } else {
    printf("  FAIL: %s\n", label);
    ++g_fail;
  }
}

/* Build a minimal valid ELF that carries `payload` as a single PT_LOAD. */
static size_t build_test_elf(uint8_t *buf, size_t buf_size, const char *payload) {
  const size_t ehdr = 52;
  const size_t phdr = 32;
  const size_t seg = ehdr + phdr;
  size_t slen = strlen(payload);
  size_t total = seg + slen;
  size_t i;

  if (total > buf_size) return 0;
  memset(buf, 0, buf_size);

  buf[0] = 0x7F; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
  buf[4] = 1; buf[5] = 1; buf[6] = 1;
  buf[16] = 2; buf[18] = 3; buf[20] = 1;
  buf[24] = 0x00; buf[25] = 0x10;
  buf[28] = (uint8_t)ehdr;
  buf[40] = (uint8_t)ehdr;
  buf[42] = (uint8_t)phdr;
  buf[44] = 1;

  buf[ehdr] = 1;
  buf[ehdr + 4] = (uint8_t)seg;
  buf[ehdr + 8] = 0x00; buf[ehdr + 9] = 0x10;
  buf[ehdr + 12] = 0x00; buf[ehdr + 13] = 0x10;
  buf[ehdr + 16] = (uint8_t)(slen & 0xFF);
  buf[ehdr + 17] = (uint8_t)((slen >> 8) & 0xFF);
  buf[ehdr + 20] = (uint8_t)(slen & 0xFF);
  buf[ehdr + 21] = (uint8_t)((slen >> 8) & 0xFF);

  for (i = 0; i < slen; ++i) {
    buf[seg + i] = (uint8_t)payload[i];
  }
  return total;
}

/* Write `len` bytes of `data` to `path`. Returns 1 on success, 0 on error. */
static int write_all(const char *path, const uint8_t *data, size_t len) {
  FILE *fp = fopen(path, "wb");
  if (fp == NULL) return 0;
  size_t n = fwrite(data, 1, len, fp);
  fclose(fp);
  return (n == len) ? 1 : 0;
}

/* Read up to `max` bytes from `path` into `out`. Returns bytes read, or 0 on error. */
static size_t read_all(const char *path, uint8_t *out, size_t max) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) return 0;
  size_t n = fread(out, 1, max, fp);
  fclose(fp);
  return n;
}

/* Build a signed SOF blob for the test payload, sharing the cert + key
 * derivation path used by kernel/fs/fs_service.c and tools/sof_wrap. */
static int build_signed(uint8_t *sof_buf, size_t sof_buf_size, size_t *sof_len_out) {
  uint8_t elf[512];
  uint8_t root_pub[32], root_priv[64];
  uint8_t inter_pub[32], inter_priv[64];
  secureos_cert_t cert;
  uint8_t cert_data[SECUREOS_CERT_TOTAL_SIZE];
  sof_build_params_t params;
  size_t elf_len;

  ed25519_create_keypair(SECUREOS_ROOT_SEED, root_pub, root_priv);
  ed25519_create_keypair(SECUREOS_INTERMEDIATE_SEED, inter_pub, inter_priv);

  cert_build(root_pub, root_priv, inter_pub, &cert);
  cert_serialize(&cert, cert_data);

  elf_len = build_test_elf(elf, sizeof(elf), "print [at-rest] hello\n");
  if (elf_len == 0) return 0;

  memset(&params, 0, sizeof(params));
  params.file_type = SOF_TYPE_BIN;
  params.name = "atrest";
  params.description = "verify-at-rest test payload";
  params.author = "SecureOS";
  params.version = "1.0";
  params.date = "2026-05-16";
  params.elf_payload = elf;
  params.elf_payload_size = elf_len;

  return (sof_build_signed(&params, inter_priv, inter_pub,
                           cert_data, SECUREOS_CERT_TOTAL_SIZE,
                           sof_buf, sof_buf_size, sof_len_out) == SOF_OK) ? 1 : 0;
}

static void test_signed_persists_and_verifies(const char *path) {
  uint8_t sof_built[2048];
  uint8_t sof_read[2048];
  size_t sof_len = 0u;
  size_t read_len = 0u;
  sof_parsed_file_t parsed;

  printf("[test_signed_persists_and_verifies]\n");

  check("build_signed ok", build_signed(sof_built, sizeof(sof_built), &sof_len) == 1);
  check("sof_len > 0", sof_len > 0);

  /* Persist to disk, then re-read into a *separate* buffer so we exercise
   * the full at-rest path (no in-memory shortcut). */
  check("write_all ok", write_all(path, sof_built, sof_len) == 1);
  read_len = read_all(path, sof_read, sizeof(sof_read));
  check("read_all matches written length", read_len == sof_len);
  check("read bytes match written bytes",
        memcmp(sof_built, sof_read, sof_len) == 0);

  check("sof_parse ok (re-read)", sof_parse(sof_read, read_len, &parsed) == SOF_OK);
  check("has_signature (re-read)", parsed.has_signature == 1);
  check("verify ok (re-read)",
        sof_verify_signature(sof_read, read_len, &parsed) == SOF_OK);
}

static void test_at_rest_signature_flip_rejected(const char *path) {
  uint8_t sof_built[2048];
  uint8_t sof_read[2048];
  size_t sof_len = 0u;
  size_t read_len = 0u;
  sof_parsed_file_t parsed;
  sof_result_t verify_result;

  printf("[test_at_rest_signature_flip_rejected]\n");

  check("build_signed ok", build_signed(sof_built, sizeof(sof_built), &sof_len) == 1);
  check("write_all ok", write_all(path, sof_built, sof_len) == 1);
  read_len = read_all(path, sof_read, sizeof(sof_read));
  check("read_all matches written length", read_len == sof_len);

  /* Locate the signature in the on-disk image via sof_parse, then flip a
   * single byte inside the signature region of the re-read buffer. */
  check("sof_parse ok (pre-flip)", sof_parse(sof_read, read_len, &parsed) == SOF_OK);
  check("has_signature (pre-flip)", parsed.has_signature == 1);

  /* The signature section is [cert (132 bytes)][ed25519 sig (64 bytes)].
   * Flip a byte inside the trailing Ed25519 signature region (last byte of
   * the section) so cert parsing still succeeds and the verify failure is
   * unambiguously the signature comparison, not cert parse. */
  size_t sig_offset = parsed.header.sig_offset;
  size_t sig_size = parsed.header.sig_size;
  check("sig_offset in bounds", sig_offset > 0 && sig_offset + sig_size <= read_len);
  check("sig_size = cert + ed25519",
        sig_size == SECUREOS_CERT_TOTAL_SIZE + ED25519_SIGNATURE_SIZE);

  size_t flip_at = sig_offset + sig_size - 1u;
  sof_read[flip_at] ^= 0x01;

  /* Re-parse to refresh the parsed view, then verify must fail. */
  check("sof_parse ok (post-flip)", sof_parse(sof_read, read_len, &parsed) == SOF_OK);
  verify_result = sof_verify_signature(sof_read, read_len, &parsed);
  check("verify rejects bit-flipped on-disk signature",
        verify_result != SOF_OK);
}

static void test_at_rest_payload_flip_rejected(const char *path) {
  uint8_t sof_built[2048];
  uint8_t sof_read[2048];
  size_t sof_len = 0u;
  size_t read_len = 0u;
  sof_parsed_file_t parsed;

  printf("[test_at_rest_payload_flip_rejected]\n");

  check("build_signed ok", build_signed(sof_built, sizeof(sof_built), &sof_len) == 1);
  check("write_all ok", write_all(path, sof_built, sof_len) == 1);
  read_len = read_all(path, sof_read, sizeof(sof_read));
  check("read_all matches written length", read_len == sof_len);

  check("sof_parse ok (pre-flip)", sof_parse(sof_read, read_len, &parsed) == SOF_OK);
  check("has_signature (pre-flip)", parsed.has_signature == 1);

  /* Flip a payload byte (inside the ELF region) — verify must fail. */
  size_t payload_offset = parsed.header.payload_offset;
  size_t payload_size = parsed.header.payload_size;
  check("payload_offset in bounds",
        payload_offset > 0 && payload_offset + payload_size <= read_len);
  check("payload non-empty", payload_size > 0);

  sof_read[payload_offset] ^= 0x01;

  check("sof_parse ok (post-flip)", sof_parse(sof_read, read_len, &parsed) == SOF_OK);
  check("verify rejects bit-flipped on-disk payload",
        sof_verify_signature(sof_read, read_len, &parsed) != SOF_OK);
}

int main(int argc, char **argv) {
  const char *path = "/tmp/sof_verify_at_rest_test.sof";
  if (argc > 1) path = argv[1];

  printf("TEST:START:sof_verify_at_rest\n");

  test_signed_persists_and_verifies(path);
  test_at_rest_signature_flip_rejected(path);
  test_at_rest_payload_flip_rejected(path);

  (void)remove(path);

  printf("Results: %d passed, %d failed\n", g_pass, g_fail);

  if (g_fail == 0) {
    printf("TEST:PASS:sof_verify_at_rest\n");
    return 0;
  }
  printf("TEST:FAIL:sof_verify_at_rest\n");
  return 1;
}
