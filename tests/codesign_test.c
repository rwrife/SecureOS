/**
 * @file codesign_test.c
 * @brief Tests for Ed25519 code signing integration in process loading.
 *
 * Purpose:
 *   Validates the codesign enforcement logic: signed binaries pass,
 *   unsigned binaries trigger consent or block, invalid signatures
 *   are always blocked. Tests sof_build_signed round-trip and
 *   sof_verify_signature with chain validation against the root key.
 *
 * Interactions:
 *   - kernel/format/sof.c: SOF build/parse/verify functions.
 *   - kernel/crypto/sha512.c, ed25519.c, cert.c: crypto primitives.
 *   - kernel/crypto/root_key.h: root/intermediate seeds.
 *
 * Launched by:
 *   build/scripts/test_codesign.sh compiles and runs this test.
 */

#include <stdio.h>
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

/* Build a minimal ELF containing a script payload */
static size_t build_test_elf(uint8_t *buf, size_t buf_size, const char *script) {
  const size_t ehdr = 52;
  const size_t phdr = 32;
  const size_t seg = ehdr + phdr;
  size_t slen = strlen(script);
  size_t total = seg + slen;
  size_t i;

  if (total > buf_size) return 0;
  memset(buf, 0, buf_size);

  buf[0] = 0x7F; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
  buf[4] = 1; buf[5] = 1; buf[6] = 1;
  /* e_type=2, e_machine=3, e_version=1 */
  buf[16] = 2; buf[18] = 3; buf[20] = 1;
  /* e_entry */
  buf[24] = 0x00; buf[25] = 0x10;
  /* e_phoff = 52 */
  buf[28] = (uint8_t)ehdr;
  /* e_ehsize = 52 */
  buf[40] = (uint8_t)ehdr;
  /* e_phentsize = 32 */
  buf[42] = (uint8_t)phdr;
  /* e_phnum = 1 */
  buf[44] = 1;

  /* PT_LOAD = 1 */
  buf[ehdr] = 1;
  /* p_offset */
  buf[ehdr + 4] = (uint8_t)seg;
  /* p_vaddr, p_paddr */
  buf[ehdr + 8] = 0x00; buf[ehdr + 9] = 0x10;
  buf[ehdr + 12] = 0x00; buf[ehdr + 13] = 0x10;
  /* p_filesz */
  buf[ehdr + 16] = (uint8_t)(slen & 0xFF);
  buf[ehdr + 17] = (uint8_t)((slen >> 8) & 0xFF);
  /* p_memsz */
  buf[ehdr + 20] = (uint8_t)(slen & 0xFF);
  buf[ehdr + 21] = (uint8_t)((slen >> 8) & 0xFF);

  for (i = 0; i < slen; ++i) {
    buf[seg + i] = (uint8_t)script[i];
  }

  return total;
}

static void test_signed_roundtrip(void) {
  uint8_t elf[512];
  uint8_t sof_buf[2048];
  size_t elf_len, sof_len;
  sof_build_params_t params;
  sof_parsed_file_t parsed;
  sof_result_t result;

  /* Derive keys */
  uint8_t root_pub[32], root_priv[64];
  uint8_t inter_pub[32], inter_priv[64];
  secureos_cert_t cert;
  uint8_t cert_data[SECUREOS_CERT_SIZE];

  printf("[test_signed_roundtrip]\n");

  ed25519_create_keypair(root_pub, root_priv, SECUREOS_ROOT_SEED);
  ed25519_create_keypair(inter_pub, inter_priv, SECUREOS_INTERMEDIATE_SEED);

  check("cert_build", cert_build(&cert, root_priv, root_pub, inter_pub) == CERT_OK);
  check("cert_serialize", cert_serialize(&cert, cert_data, sizeof(cert_data)) == CERT_OK);

  elf_len = build_test_elf(elf, sizeof(elf), "print hello\n");
  check("elf_built", elf_len > 0);

  memset(&params, 0, sizeof(params));
  params.file_type = SOF_TYPE_BIN;
  params.name = "test";
  params.description = "test binary";
  params.author = "test";
  params.version = "1.0";
  params.date = "2026-01-01";
  params.elf_payload = elf;
  params.elf_payload_size = elf_len;

  result = sof_build_signed(&params, inter_priv, inter_pub,
                             cert_data, SECUREOS_CERT_SIZE,
                             sof_buf, sizeof(sof_buf), &sof_len);
  check("sof_build_signed ok", result == SOF_OK);
  check("sof_len > 0", sof_len > 0);

  result = sof_parse(sof_buf, sof_len, &parsed);
  check("sof_parse ok", result == SOF_OK);
  check("has_signature", parsed.has_signature == 1);

  result = sof_verify_signature(sof_buf, sof_len, &parsed);
  check("sof_verify_signature ok", result == SOF_OK);
}

static void test_unsigned_no_signature(void) {
  uint8_t elf[512];
  uint8_t sof_buf[2048];
  size_t elf_len, sof_len;
  sof_build_params_t params;
  sof_parsed_file_t parsed;
  sof_result_t result;

  printf("[test_unsigned_no_signature]\n");

  elf_len = build_test_elf(elf, sizeof(elf), "print test\n");

  memset(&params, 0, sizeof(params));
  params.file_type = SOF_TYPE_BIN;
  params.name = "unsigned";
  params.description = "unsigned binary";
  params.author = "test";
  params.version = "1.0";
  params.date = "2026-01-01";
  params.elf_payload = elf;
  params.elf_payload_size = elf_len;

  result = sof_build(&params, sof_buf, sizeof(sof_buf), &sof_len);
  check("sof_build ok", result == SOF_OK);

  result = sof_parse(sof_buf, sof_len, &parsed);
  check("sof_parse ok", result == SOF_OK);
  check("no_signature", parsed.has_signature == 0);

  /* verify_signature should return OK for unsigned (no sig to fail) */
  result = sof_verify_signature(sof_buf, sof_len, &parsed);
  check("verify unsigned returns ok", result == SOF_OK);
}

static void test_corrupted_signature_blocked(void) {
  uint8_t elf[512];
  uint8_t sof_buf[2048];
  size_t elf_len, sof_len;
  sof_build_params_t params;
  sof_parsed_file_t parsed;
  sof_result_t result;

  uint8_t root_pub[32], root_priv[64];
  uint8_t inter_pub[32], inter_priv[64];
  secureos_cert_t cert;
  uint8_t cert_data[SECUREOS_CERT_SIZE];

  printf("[test_corrupted_signature_blocked]\n");

  ed25519_create_keypair(root_pub, root_priv, SECUREOS_ROOT_SEED);
  ed25519_create_keypair(inter_pub, inter_priv, SECUREOS_INTERMEDIATE_SEED);
  cert_build(&cert, root_priv, root_pub, inter_pub);
  cert_serialize(&cert, cert_data, sizeof(cert_data));

  elf_len = build_test_elf(elf, sizeof(elf), "print corrupt\n");

  memset(&params, 0, sizeof(params));
  params.file_type = SOF_TYPE_BIN;
  params.name = "corrupt";
  params.description = "corrupt test";
  params.author = "test";
  params.version = "1.0";
  params.date = "2026-01-01";
  params.elf_payload = elf;
  params.elf_payload_size = elf_len;

  sof_build_signed(&params, inter_priv, inter_pub,
                    cert_data, SECUREOS_CERT_SIZE,
                    sof_buf, sizeof(sof_buf), &sof_len);

  sof_parse(sof_buf, sof_len, &parsed);
  check("has_signature before corrupt", parsed.has_signature == 1);

  /* Corrupt the signature (last 64 bytes of sig section) */
  if (parsed.header.sig_offset > 0 && parsed.header.sig_size > 0) {
    size_t sig_end = parsed.header.sig_offset + parsed.header.sig_size;
    if (sig_end <= sof_len && sig_end > 2) {
      sof_buf[sig_end - 1] ^= 0xFF;
      sof_buf[sig_end - 2] ^= 0xFF;
    }
  }

  /* Re-parse after corruption */
  result = sof_parse(sof_buf, sof_len, &parsed);
  check("sof_parse after corrupt ok", result == SOF_OK);

  result = sof_verify_signature(sof_buf, sof_len, &parsed);
  check("verify corrupted sig fails", result != SOF_OK);
}

static void test_wrong_key_signature_blocked(void) {
  uint8_t elf[512];
  uint8_t sof_buf[2048];
  size_t elf_len, sof_len;
  sof_build_params_t params;
  sof_parsed_file_t parsed;
  sof_result_t result;

  /* Sign with a random key not chained to root */
  uint8_t rogue_seed[32] = {0xDE, 0xAD, 0xBE, 0xEF};
  uint8_t rogue_pub[32], rogue_priv[64];
  uint8_t root_pub[32], root_priv[64];
  secureos_cert_t cert;
  uint8_t cert_data[SECUREOS_CERT_SIZE];

  printf("[test_wrong_key_signature_blocked]\n");

  ed25519_create_keypair(rogue_pub, rogue_priv, rogue_seed);
  ed25519_create_keypair(root_pub, root_priv, SECUREOS_ROOT_SEED);

  /* Build a cert signed by root but for rogue key */
  cert_build(&cert, root_priv, root_pub, rogue_pub);
  cert_serialize(&cert, cert_data, sizeof(cert_data));

  elf_len = build_test_elf(elf, sizeof(elf), "print rogue\n");

  memset(&params, 0, sizeof(params));
  params.file_type = SOF_TYPE_BIN;
  params.name = "rogue";
  params.description = "rogue key test";
  params.author = "test";
  params.version = "1.0";
  params.date = "2026-01-01";
  params.elf_payload = elf;
  params.elf_payload_size = elf_len;

  /* Sign with rogue key + valid cert for rogue → this should verify OK
     because the cert is root-signed for rogue_pub, and rogue signs the payload */
  result = sof_build_signed(&params, rogue_priv, rogue_pub,
                             cert_data, SECUREOS_CERT_SIZE,
                             sof_buf, sizeof(sof_buf), &sof_len);
  check("sof_build_signed with rogue ok", result == SOF_OK);

  result = sof_parse(sof_buf, sof_len, &parsed);
  check("sof_parse ok", result == SOF_OK);

  /* The cert IS valid (root signed it for rogue), so this verifies */
  result = sof_verify_signature(sof_buf, sof_len, &parsed);
  check("rogue-but-root-signed cert verifies", result == SOF_OK);

  /* Now test with a self-signed cert (not chained to root) */
  cert_build(&cert, rogue_priv, rogue_pub, rogue_pub);
  cert_serialize(&cert, cert_data, sizeof(cert_data));

  result = sof_build_signed(&params, rogue_priv, rogue_pub,
                             cert_data, SECUREOS_CERT_SIZE,
                             sof_buf, sizeof(sof_buf), &sof_len);
  check("sof_build_signed self-signed ok", result == SOF_OK);

  result = sof_parse(sof_buf, sof_len, &parsed);
  check("sof_parse self-signed ok", result == SOF_OK);

  result = sof_verify_signature(sof_buf, sof_len, &parsed);
  check("self-signed cert rejected", result != SOF_OK);
}

static void test_signed_library(void) {
  uint8_t elf[512];
  uint8_t sof_buf[2048];
  size_t elf_len, sof_len;
  sof_build_params_t params;
  sof_parsed_file_t parsed;
  sof_result_t result;

  uint8_t root_pub[32], root_priv[64];
  uint8_t inter_pub[32], inter_priv[64];
  secureos_cert_t cert;
  uint8_t cert_data[SECUREOS_CERT_SIZE];

  printf("[test_signed_library]\n");

  ed25519_create_keypair(root_pub, root_priv, SECUREOS_ROOT_SEED);
  ed25519_create_keypair(inter_pub, inter_priv, SECUREOS_INTERMEDIATE_SEED);
  cert_build(&cert, root_priv, root_pub, inter_pub);
  cert_serialize(&cert, cert_data, sizeof(cert_data));

  elf_len = build_test_elf(elf, sizeof(elf), "print lib\n");

  memset(&params, 0, sizeof(params));
  params.file_type = SOF_TYPE_LIB;
  params.name = "testlib";
  params.description = "test library";
  params.author = "test";
  params.version = "1.0";
  params.date = "2026-01-01";
  params.elf_payload = elf;
  params.elf_payload_size = elf_len;

  result = sof_build_signed(&params, inter_priv, inter_pub,
                             cert_data, SECUREOS_CERT_SIZE,
                             sof_buf, sizeof(sof_buf), &sof_len);
  check("lib sof_build_signed ok", result == SOF_OK);

  result = sof_parse(sof_buf, sof_len, &parsed);
  check("lib sof_parse ok", result == SOF_OK);
  check("lib has_signature", parsed.has_signature == 1);
  check("lib file_type", parsed.header.file_type == SOF_TYPE_LIB);

  result = sof_verify_signature(sof_buf, sof_len, &parsed);
  check("lib verify ok", result == SOF_OK);
}

int main(void) {
  printf("TEST:START:codesign\n");

  test_signed_roundtrip();
  test_unsigned_no_signature();
  test_corrupted_signature_blocked();
  test_wrong_key_signature_blocked();
  test_signed_library();

  printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);

  if (g_fail > 0) {
    printf("TEST:FAIL:codesign\n");
    return 1;
  }

  printf("TEST:PASS:codesign\n");
  return 0;
}