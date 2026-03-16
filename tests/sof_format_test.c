/**
 * @file sof_format_test.c
 * @brief Unit tests for the SecureOS File Format (SOF) library.
 *
 * Purpose:
 *   Validates the SOF container format parsing, building, metadata lookup,
 *   signature stubs, and error handling.  Tests round-trip build→parse
 *   consistency and rejection of malformed inputs.
 *
 * Interactions:
 *   - kernel/format/sof.h and kernel/format/sof.c provide the SOF API.
 *
 * Launched by:
 *   Compiled and run by build/scripts/test_sof_format.sh via the Docker
 *   toolchain.
 */

#include <stdint.h>
#include <stddef.h>

#include "../kernel/format/sof.h"

/* ---- Minimal test harness ---------------------------------------------- */

static int test_pass_count = 0;
static int test_fail_count = 0;

static int str_equals(const char *a, const char *b) {
  if (a == 0 || b == 0) {
    return a == b;
  }
  while (*a != '\0' && *b != '\0') {
    if (*a != *b) { return 0; }
    ++a;
    ++b;
  }
  return *a == *b;
}

static size_t str_len(const char *s) {
  size_t n = 0u;
  if (s == 0) { return 0u; }
  while (s[n] != '\0') { ++n; }
  return n;
}

static void str_copy(char *dst, size_t dst_size, const char *src) {
  size_t i = 0u;
  if (dst == 0 || dst_size == 0u) { return; }
  while (src[i] != '\0' && i + 1u < dst_size) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

/* Simple serial-style output for test results (stub for hosted environment) */
#ifdef __linux__
#include <stdio.h>
#define TEST_PRINT(msg) fputs((msg), stdout)
#else
static void test_print_stub(const char *msg) { (void)msg; }
#define TEST_PRINT(msg) test_print_stub(msg)
#endif

static void test_assert(int condition, const char *name) {
  if (condition) {
    TEST_PRINT("  PASS: ");
    TEST_PRINT(name);
    TEST_PRINT("\n");
    ++test_pass_count;
  } else {
    TEST_PRINT("  FAIL: ");
    TEST_PRINT(name);
    TEST_PRINT("\n");
    ++test_fail_count;
  }
}

/* ---- Sample ELF payload for testing ------------------------------------ */

/*
 * Minimal valid ELF32 header (52 bytes) + program header (32 bytes) +
 * small script payload.  This matches the format produced by
 * fs_build_script_elf() in fs_service.c.
 */
static void build_test_elf(uint8_t *buf, size_t buf_size, size_t *out_len) {
  const char *script = "print hello\n";
  size_t script_len = str_len(script);
  const size_t ehdr_size = 52u;
  const size_t phdr_size = 32u;
  const size_t seg_offset = ehdr_size + phdr_size;
  size_t i = 0u;

  if (seg_offset + script_len > buf_size) {
    *out_len = 0u;
    return;
  }

  /* Zero buffer */
  for (i = 0u; i < buf_size; ++i) { buf[i] = 0u; }

  /* ELF magic */
  buf[0] = 0x7Fu; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
  buf[4] = 1u; /* 32-bit */
  buf[5] = 1u; /* little-endian */
  buf[6] = 1u; /* ELF version */

  /* e_type = ET_EXEC */
  buf[16] = 2u; buf[17] = 0u;
  /* e_machine = EM_386 */
  buf[18] = 3u; buf[19] = 0u;
  /* e_version */
  buf[20] = 1u;
  /* e_entry */
  buf[24] = 0x00; buf[25] = 0x10;
  /* e_phoff = ehdr_size */
  buf[28] = (uint8_t)ehdr_size;
  /* e_ehsize */
  buf[40] = (uint8_t)ehdr_size;
  /* e_phentsize */
  buf[42] = (uint8_t)phdr_size;
  /* e_phnum = 1 */
  buf[44] = 1u;

  /* Program header: PT_LOAD */
  buf[ehdr_size + 0u] = 1u; /* p_type = PT_LOAD */
  buf[ehdr_size + 4u] = (uint8_t)seg_offset; /* p_offset */
  buf[ehdr_size + 8u] = 0x00; buf[ehdr_size + 9u] = 0x10; /* p_vaddr */
  buf[ehdr_size + 12u] = 0x00; buf[ehdr_size + 13u] = 0x10; /* p_paddr */
  buf[ehdr_size + 16u] = (uint8_t)script_len; /* p_filesz */
  buf[ehdr_size + 20u] = (uint8_t)script_len; /* p_memsz */
  buf[ehdr_size + 24u] = 4u; /* p_flags = PF_R */
  buf[ehdr_size + 28u] = 1u; /* p_align */

  /* Script payload */
  for (i = 0u; i < script_len; ++i) {
    buf[seg_offset + i] = (uint8_t)script[i];
  }

  *out_len = seg_offset + script_len;
}

/* ---- Test cases -------------------------------------------------------- */

static void test_sof_build_and_parse_bin(void) {
  uint8_t elf_buf[256];
  uint8_t sof_buf[1024];
  size_t elf_len = 0u;
  size_t sof_len = 0u;
  sof_parsed_file_t parsed;
  sof_result_t result;
  const char *val = 0;
  size_t val_len = 0u;

  build_test_elf(elf_buf, sizeof(elf_buf), &elf_len);
  test_assert(elf_len > 0u, "build_test_elf produced output");

  sof_build_params_t params;
  params.file_type = SOF_TYPE_BIN;
  params.name = "test-bin";
  params.description = "A test binary";
  params.author = "SecureOS";
  params.version = "1.0.0";
  params.date = "2026-03-16";
  params.icon = 0;
  params.elf_payload = elf_buf;
  params.elf_payload_size = elf_len;

  result = sof_build(&params, sof_buf, sizeof(sof_buf), &sof_len);
  test_assert(result == SOF_OK, "sof_build returns SOF_OK for BIN");
  test_assert(sof_len > sizeof(sof_header_t), "sof_build output > header size");

  result = sof_parse(sof_buf, sof_len, &parsed);
  test_assert(result == SOF_OK, "sof_parse returns SOF_OK");
  test_assert(parsed.header.file_type == SOF_TYPE_BIN, "parsed file_type is BIN");
  test_assert(parsed.header.format_version == 1u, "parsed format_version is 1");
  test_assert(parsed.payload_size == elf_len, "parsed payload_size matches ELF len");

  result = sof_get_meta(&parsed, SOF_META_NAME, &val, &val_len);
  test_assert(result == SOF_OK, "sof_get_meta NAME found");
  test_assert(str_equals(val, "test-bin"), "NAME value matches");

  result = sof_get_meta(&parsed, SOF_META_AUTHOR, &val, &val_len);
  test_assert(result == SOF_OK, "sof_get_meta AUTHOR found");
  test_assert(str_equals(val, "SecureOS"), "AUTHOR value matches");

  result = sof_get_meta(&parsed, SOF_META_VERSION, &val, &val_len);
  test_assert(result == SOF_OK, "sof_get_meta VERSION found");
  test_assert(str_equals(val, "1.0.0"), "VERSION value matches");

  result = sof_get_meta(&parsed, SOF_META_DATE, &val, &val_len);
  test_assert(result == SOF_OK, "sof_get_meta DATE found");
  test_assert(str_equals(val, "2026-03-16"), "DATE value matches");

  result = sof_get_meta(&parsed, SOF_META_DESCRIPTION, &val, &val_len);
  test_assert(result == SOF_OK, "sof_get_meta DESCRIPTION found");
  test_assert(str_equals(val, "A test binary"), "DESCRIPTION value matches");
}

static void test_sof_build_and_parse_lib(void) {
  uint8_t elf_buf[256];
  uint8_t sof_buf[1024];
  size_t elf_len = 0u;
  size_t sof_len = 0u;
  sof_parsed_file_t parsed;
  sof_result_t result;

  build_test_elf(elf_buf, sizeof(elf_buf), &elf_len);

  sof_build_params_t params;
  params.file_type = SOF_TYPE_LIB;
  params.name = "test-lib";
  params.description = "A test library";
  params.author = "SecureOS";
  params.version = "1.0.0";
  params.date = "2026-03-16";
  params.icon = 0;
  params.elf_payload = elf_buf;
  params.elf_payload_size = elf_len;

  result = sof_build(&params, sof_buf, sizeof(sof_buf), &sof_len);
  test_assert(result == SOF_OK, "sof_build returns SOF_OK for LIB");

  result = sof_parse(sof_buf, sof_len, &parsed);
  test_assert(result == SOF_OK, "sof_parse LIB returns SOF_OK");
  test_assert(parsed.header.file_type == SOF_TYPE_LIB, "parsed file_type is LIB");
}

static void test_sof_is_sof(void) {
  uint8_t elf_buf[256];
  uint8_t sof_buf[1024];
  size_t elf_len = 0u;
  size_t sof_len = 0u;
  uint8_t garbage[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00};

  build_test_elf(elf_buf, sizeof(elf_buf), &elf_len);

  sof_build_params_t params;
  params.file_type = SOF_TYPE_BIN;
  params.name = "test";
  params.description = 0;
  params.author = 0;
  params.version = 0;
  params.date = 0;
  params.icon = 0;
  params.elf_payload = elf_buf;
  params.elf_payload_size = elf_len;

  (void)sof_build(&params, sof_buf, sizeof(sof_buf), &sof_len);

  test_assert(sof_is_sof(sof_buf, sof_len) == 1, "sof_is_sof true for valid SOF");
  test_assert(sof_is_sof(elf_buf, elf_len) == 0, "sof_is_sof false for raw ELF");
  test_assert(sof_is_sof(garbage, sizeof(garbage)) == 0, "sof_is_sof false for garbage");
  test_assert(sof_is_sof(sof_buf, 4) == 0, "sof_is_sof false for truncated buffer");
}

static void test_sof_reject_bad_magic(void) {
  uint8_t elf_buf[256];
  uint8_t sof_buf[1024];
  size_t elf_len = 0u;
  size_t sof_len = 0u;
  sof_parsed_file_t parsed;

  build_test_elf(elf_buf, sizeof(elf_buf), &elf_len);

  sof_build_params_t params;
  params.file_type = SOF_TYPE_BIN;
  params.name = "test";
  params.description = 0;
  params.author = 0;
  params.version = 0;
  params.date = 0;
  params.icon = 0;
  params.elf_payload = elf_buf;
  params.elf_payload_size = elf_len;

  (void)sof_build(&params, sof_buf, sizeof(sof_buf), &sof_len);

  /* Corrupt magic */
  sof_buf[0] = 0xFFu;
  test_assert(sof_parse(sof_buf, sof_len, &parsed) == SOF_ERR_INVALID_MAGIC,
              "bad magic rejected");
}

static void test_sof_reject_bad_version(void) {
  uint8_t elf_buf[256];
  uint8_t sof_buf[1024];
  size_t elf_len = 0u;
  size_t sof_len = 0u;
  sof_parsed_file_t parsed;

  build_test_elf(elf_buf, sizeof(elf_buf), &elf_len);

  sof_build_params_t params;
  params.file_type = SOF_TYPE_BIN;
  params.name = "test";
  params.description = 0;
  params.author = 0;
  params.version = 0;
  params.date = 0;
  params.icon = 0;
  params.elf_payload = elf_buf;
  params.elf_payload_size = elf_len;

  (void)sof_build(&params, sof_buf, sizeof(sof_buf), &sof_len);

  /* Corrupt version */
  sof_buf[4] = 99u;
  test_assert(sof_parse(sof_buf, sof_len, &parsed) == SOF_ERR_INVALID_VERSION,
              "bad version rejected");
}

static void test_sof_reject_truncated(void) {
  uint8_t small_buf[16] = {0x53, 0x45, 0x4F, 0x53, 1, 1, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 0};
  sof_parsed_file_t parsed;

  test_assert(sof_parse(small_buf, sizeof(small_buf), &parsed) == SOF_ERR_INVALID_SIZE,
              "truncated buffer rejected");
}

static void test_sof_reject_bad_type(void) {
  uint8_t elf_buf[256];
  uint8_t sof_buf[1024];
  size_t elf_len = 0u;
  size_t sof_len = 0u;
  sof_parsed_file_t parsed;

  build_test_elf(elf_buf, sizeof(elf_buf), &elf_len);

  sof_build_params_t params;
  params.file_type = SOF_TYPE_BIN;
  params.name = "test";
  params.description = 0;
  params.author = 0;
  params.version = 0;
  params.date = 0;
  params.icon = 0;
  params.elf_payload = elf_buf;
  params.elf_payload_size = elf_len;

  (void)sof_build(&params, sof_buf, sizeof(sof_buf), &sof_len);

  /* Corrupt file type to invalid value */
  sof_buf[5] = 0xFFu;
  test_assert(sof_parse(sof_buf, sof_len, &parsed) == SOF_ERR_INVALID_TYPE,
              "bad file type rejected");
}

static void test_sof_get_meta_not_found(void) {
  uint8_t elf_buf[256];
  uint8_t sof_buf[1024];
  size_t elf_len = 0u;
  size_t sof_len = 0u;
  sof_parsed_file_t parsed;
  const char *val = 0;
  size_t val_len = 0u;

  build_test_elf(elf_buf, sizeof(elf_buf), &elf_len);

  sof_build_params_t params;
  params.file_type = SOF_TYPE_BIN;
  params.name = "test";
  params.description = 0;
  params.author = 0;
  params.version = 0;
  params.date = 0;
  params.icon = 0;
  params.elf_payload = elf_buf;
  params.elf_payload_size = elf_len;

  (void)sof_build(&params, sof_buf, sizeof(sof_buf), &sof_len);
  (void)sof_parse(sof_buf, sof_len, &parsed);

  test_assert(sof_get_meta(&parsed, SOF_META_ICON, &val, &val_len) == SOF_ERR_INVALID_META,
              "missing ICON meta returns error");
  test_assert(sof_get_meta(&parsed, SOF_META_AUTHOR, &val, &val_len) == SOF_ERR_INVALID_META,
              "missing AUTHOR meta returns error");
}

static void test_sof_signature_stub(void) {
  uint8_t elf_buf[256];
  uint8_t sof_buf[1024];
  size_t elf_len = 0u;
  size_t sof_len = 0u;
  sof_parsed_file_t parsed;

  build_test_elf(elf_buf, sizeof(elf_buf), &elf_len);

  sof_build_params_t params;
  params.file_type = SOF_TYPE_BIN;
  params.name = "test";
  params.description = 0;
  params.author = 0;
  params.version = 0;
  params.date = 0;
  params.icon = 0;
  params.elf_payload = elf_buf;
  params.elf_payload_size = elf_len;

  (void)sof_build(&params, sof_buf, sizeof(sof_buf), &sof_len);
  (void)sof_parse(sof_buf, sof_len, &parsed);

  test_assert(sof_signature_present(&parsed.header) == 0, "signature not present");
  test_assert(sof_verify_signature(sof_buf, sof_len, &parsed) == SOF_OK,
              "verify_signature stub returns OK");
}

static void test_sof_round_trip_payload(void) {
  uint8_t elf_buf[256];
  uint8_t sof_buf[1024];
  size_t elf_len = 0u;
  size_t sof_len = 0u;
  sof_parsed_file_t parsed;
  size_t i = 0u;
  int match = 1;

  build_test_elf(elf_buf, sizeof(elf_buf), &elf_len);

  sof_build_params_t params;
  params.file_type = SOF_TYPE_BIN;
  params.name = "roundtrip";
  params.description = 0;
  params.author = 0;
  params.version = 0;
  params.date = 0;
  params.icon = 0;
  params.elf_payload = elf_buf;
  params.elf_payload_size = elf_len;

  (void)sof_build(&params, sof_buf, sizeof(sof_buf), &sof_len);
  (void)sof_parse(sof_buf, sof_len, &parsed);

  test_assert(parsed.payload_size == elf_len, "round-trip payload size matches");

  for (i = 0u; i < elf_len; ++i) {
    if (parsed.payload[i] != elf_buf[i]) {
      match = 0;
      break;
    }
  }
  test_assert(match, "round-trip payload bytes match");
}

static void test_sof_app_bundle_stub(void) {
  uint8_t elf_buf[256];
  uint8_t sof_buf[1024];
  size_t elf_len = 0u;
  size_t sof_len = 0u;
  sof_parsed_file_t parsed;

  build_test_elf(elf_buf, sizeof(elf_buf), &elf_len);

  /* Build a valid BIN, then try to parse as app bundle */
  sof_build_params_t params;
  params.file_type = SOF_TYPE_BIN;
  params.name = "test";
  params.description = 0;
  params.author = 0;
  params.version = 0;
  params.date = 0;
  params.icon = 0;
  params.elf_payload = elf_buf;
  params.elf_payload_size = elf_len;

  (void)sof_build(&params, sof_buf, sizeof(sof_buf), &sof_len);

  /* BIN file should be rejected by app bundle parser */
  test_assert(sof_parse_app_bundle(sof_buf, sof_len, &parsed) == SOF_ERR_INVALID_TYPE,
              "app_bundle rejects BIN type");

  /* Manually set type to APP and try again — still unsupported */
  sof_buf[5] = SOF_TYPE_APP;
  test_assert(sof_parse_app_bundle(sof_buf, sof_len, &parsed) == SOF_ERR_INVALID_TYPE,
              "app_bundle returns unsupported for APP type");
}

/* ---- Main -------------------------------------------------------------- */

int main(void) {
  TEST_PRINT("TEST:START:sof_format\n");

  test_sof_build_and_parse_bin();
  test_sof_build_and_parse_lib();
  test_sof_is_sof();
  test_sof_reject_bad_magic();
  test_sof_reject_bad_version();
  test_sof_reject_truncated();
  test_sof_reject_bad_type();
  test_sof_get_meta_not_found();
  test_sof_signature_stub();
  test_sof_round_trip_payload();
  test_sof_app_bundle_stub();

  if (test_fail_count == 0) {
    TEST_PRINT("TEST:PASS:sof_format\n");
  } else {
    TEST_PRINT("TEST:FAIL:sof_format\n");
  }

  return test_fail_count > 0 ? 1 : 0;
}