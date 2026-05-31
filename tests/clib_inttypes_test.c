/**
 * @file clib_inttypes_test.c
 * @brief Host unit test for the freestanding <inttypes.h> format-string
 *        nucleus (issue #407 slice 11, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Covers:
 *   1. Each PRI* macro expands to a non-empty string literal whose final
 *      character is the spec-mandated conversion ('d','i','u','o','x','X').
 *   2. Each SCN* macro expands to a non-empty string literal whose final
 *      character is the spec-mandated conversion ('d','i','u','o','x').
 *   3. Round-trip: snprintf with a PRI*<n> macro of an intN_t / uintN_t
 *      value reproduces the canonical decimal / hex / octal spelling.
 *   4. Width-class agreement: PRI*LEAST<n> / PRI*FAST<n> resolve to the
 *      same macro the unit test would use for the matching exact-width
 *      typedef (slice 10b parity).
 *   5. PRIdMAX / PRIuMAX / PRIdPTR / PRIuPTR round-trip an intmax_t and
 *      a uintptr_t value.
 *   6. Drift anchor: clib_inttypes_fmt() in the LINKED libclib returns
 *      the same string this TU's #include sees (`symbol_set_pinned`).
 *
 * Compiled with `-fno-builtin` so the assertions exercise OUR header
 * macros rather than `__builtin_*` shortcuts. The host's <stdio.h>
 * (used here for `snprintf` / `fprintf` only — the test's own scaffolding,
 * not the surface under test) IS pulled in deliberately, since the
 * format-string macros are useless without a printf at the other end
 * and the host's printf already implements the canonical conversion
 * spellings.
 *
 * Launched by:
 *   build/scripts/test_clib_inttypes.sh (dispatched via
 *   build/scripts/test.sh clib_inttypes).
 */

#include "../user/libs/clib/include/clib/inttypes.h"
#include "../user/libs/clib/include/clib/stdint.h"
#include "../user/libs/clib/include/clib/errno.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:clib_inttypes:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* ---- 1+2. macro shape -------------------------------------------------- */
static int last_char_is(const char *s, char want) {
  if (s == NULL || s[0] == '\0') return 0;
  size_t n = 0;
  while (s[n] != '\0') ++n;
  return s[n - 1] == want;
}

static void check_macro_shape(void) {
  /* PRI* — final conversion character per C11 §7.8.1. */
  CHECK(last_char_is(PRId8,   'd'), "PRId8_ends_d");
  CHECK(last_char_is(PRId16,  'd'), "PRId16_ends_d");
  CHECK(last_char_is(PRId32,  'd'), "PRId32_ends_d");
  CHECK(last_char_is(PRId64,  'd'), "PRId64_ends_d");
  CHECK(last_char_is(PRIi32,  'i'), "PRIi32_ends_i");
  CHECK(last_char_is(PRIu32,  'u'), "PRIu32_ends_u");
  CHECK(last_char_is(PRIu64,  'u'), "PRIu64_ends_u");
  CHECK(last_char_is(PRIo32,  'o'), "PRIo32_ends_o");
  CHECK(last_char_is(PRIx32,  'x'), "PRIx32_ends_x");
  CHECK(last_char_is(PRIx64,  'x'), "PRIx64_ends_x");
  CHECK(last_char_is(PRIX32,  'X'), "PRIX32_ends_X");
  CHECK(last_char_is(PRIX64,  'X'), "PRIX64_ends_X");
  CHECK(last_char_is(PRIdMAX, 'd'), "PRIdMAX_ends_d");
  CHECK(last_char_is(PRIuMAX, 'u'), "PRIuMAX_ends_u");
  CHECK(last_char_is(PRIxMAX, 'x'), "PRIxMAX_ends_x");
  CHECK(last_char_is(PRIdPTR, 'd'), "PRIdPTR_ends_d");
  CHECK(last_char_is(PRIuPTR, 'u'), "PRIuPTR_ends_u");
  CHECK(last_char_is(PRIxPTR, 'x'), "PRIxPTR_ends_x");

  /* SCN* — same shape, no uppercase X by spec. */
  CHECK(last_char_is(SCNd8,   'd'), "SCNd8_ends_d");
  CHECK(last_char_is(SCNd32,  'd'), "SCNd32_ends_d");
  CHECK(last_char_is(SCNd64,  'd'), "SCNd64_ends_d");
  CHECK(last_char_is(SCNi32,  'i'), "SCNi32_ends_i");
  CHECK(last_char_is(SCNu32,  'u'), "SCNu32_ends_u");
  CHECK(last_char_is(SCNu64,  'u'), "SCNu64_ends_u");
  CHECK(last_char_is(SCNo32,  'o'), "SCNo32_ends_o");
  CHECK(last_char_is(SCNx32,  'x'), "SCNx32_ends_x");
  CHECK(last_char_is(SCNx64,  'x'), "SCNx64_ends_x");

  /* No spec-conformant macro is empty. */
  CHECK(PRId8[0]   != '\0', "PRId8_nonempty");
  CHECK(PRIu64[0]  != '\0', "PRIu64_nonempty");
  CHECK(PRIxMAX[0] != '\0', "PRIxMAX_nonempty");
  CHECK(SCNd64[0]  != '\0', "SCNd64_nonempty");
}

/* ---- 3. printf round-trip --------------------------------------------- */
static void check_printf_roundtrip(void) {
  char buf[64];

  /* int8_t signed — value chosen to require sign + multi-digit. */
  int8_t  v8  = -42;
  /* int32_t / uint32_t / int64_t / uint64_t. */
  int32_t v32 = -2147483647;        /* one off INT32_MIN to keep literal portable */
  uint32_t u32 = 0xCAFEBABEu;
  int64_t v64 = -9000000000LL;
  uint64_t u64 = 0xDEADBEEFCAFE1234ULL;

  /* PRId8 → host printf must yield "-42". */
  snprintf(buf, sizeof(buf), "%" PRId8, (int)v8);
  CHECK(strcmp(buf, "-42") == 0, "PRId8_roundtrip_neg42");

  snprintf(buf, sizeof(buf), "%" PRId32, v32);
  CHECK(strcmp(buf, "-2147483647") == 0, "PRId32_roundtrip");

  snprintf(buf, sizeof(buf), "%" PRIu32, u32);
  CHECK(strcmp(buf, "3405691582") == 0, "PRIu32_roundtrip");

  snprintf(buf, sizeof(buf), "%" PRIx32, u32);
  CHECK(strcmp(buf, "cafebabe") == 0, "PRIx32_roundtrip");

  snprintf(buf, sizeof(buf), "%" PRIX32, u32);
  CHECK(strcmp(buf, "CAFEBABE") == 0, "PRIX32_roundtrip");

  snprintf(buf, sizeof(buf), "%" PRId64, v64);
  CHECK(strcmp(buf, "-9000000000") == 0, "PRId64_roundtrip");

  snprintf(buf, sizeof(buf), "%" PRIx64, u64);
  CHECK(strcmp(buf, "deadbeefcafe1234") == 0, "PRIx64_roundtrip");

  snprintf(buf, sizeof(buf), "%" PRIo32, 0755u);
  CHECK(strcmp(buf, "755") == 0, "PRIo32_roundtrip");
}

/* ---- 4. LEAST / FAST family agreement --------------------------------- *
 *
 * Slice 10b's least/fast typedefs are required by C11 to be at least
 * N bits wide; on every target the project ships against they alias
 * the matching exact-width type, so PRI*LEASTn / PRI*FASTn must
 * resolve to the same macro spelling as PRI*n. Pin that here so a
 * future drift cannot silently make them disagree.
 */
static void check_least_fast_format(void) {
  CHECK(strcmp(PRIdLEAST8,  PRId8)  == 0, "PRIdLEAST8_eq_PRId8");
  CHECK(strcmp(PRIdLEAST16, PRId16) == 0, "PRIdLEAST16_eq_PRId16");
  CHECK(strcmp(PRIdLEAST32, PRId32) == 0, "PRIdLEAST32_eq_PRId32");
  CHECK(strcmp(PRIdLEAST64, PRId64) == 0, "PRIdLEAST64_eq_PRId64");
  CHECK(strcmp(PRIuLEAST32, PRIu32) == 0, "PRIuLEAST32_eq_PRIu32");
  CHECK(strcmp(PRIxLEAST64, PRIx64) == 0, "PRIxLEAST64_eq_PRIx64");
  CHECK(strcmp(PRIdFAST8,   PRId8)  == 0, "PRIdFAST8_eq_PRId8");
  CHECK(strcmp(PRIdFAST32,  PRId32) == 0, "PRIdFAST32_eq_PRId32");
  CHECK(strcmp(PRIuFAST64,  PRIu64) == 0, "PRIuFAST64_eq_PRIu64");
  CHECK(strcmp(SCNdLEAST32, SCNd32) == 0, "SCNdLEAST32_eq_SCNd32");
  CHECK(strcmp(SCNuFAST64,  SCNu64) == 0, "SCNuFAST64_eq_SCNu64");
}

/* ---- 5. MAX / PTR round-trip ------------------------------------------ */
static void check_max_ptr_roundtrip(void) {
  char buf[64];

  intmax_t   imax = -1234567890123LL;
  uintmax_t  umax = 0xFEEDFACECAFEBABEULL;
  uintptr_t  p    = (uintptr_t)0x1234ABCD;

  snprintf(buf, sizeof(buf), "%" PRIdMAX, imax);
  CHECK(strcmp(buf, "-1234567890123") == 0, "PRIdMAX_roundtrip");

  snprintf(buf, sizeof(buf), "%" PRIxMAX, umax);
  CHECK(strcmp(buf, "feedfacecafebabe") == 0, "PRIxMAX_roundtrip");

  snprintf(buf, sizeof(buf), "%" PRIxPTR, p);
  CHECK(strcmp(buf, "1234abcd") == 0, "PRIxPTR_roundtrip");

  snprintf(buf, sizeof(buf), "%" PRIuPTR, (uintptr_t)42u);
  CHECK(strcmp(buf, "42") == 0, "PRIuPTR_roundtrip");
}

/* ---- 6. drift anchor: helper-TU round-trip ---------------------------- */
static void check_symbol_set_pinned(void) {
  /* Helper-TU (src/inttypes.c) was compiled separately and linked
   * against this TU. Its view of each macro must match ours, byte-
   * for-byte. */
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_PRId8),   PRId8)   == 0,
        "helper_PRId8_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_PRId16),  PRId16)  == 0,
        "helper_PRId16_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_PRId32),  PRId32)  == 0,
        "helper_PRId32_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_PRId64),  PRId64)  == 0,
        "helper_PRId64_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_PRIu32),  PRIu32)  == 0,
        "helper_PRIu32_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_PRIu64),  PRIu64)  == 0,
        "helper_PRIu64_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_PRIx32),  PRIx32)  == 0,
        "helper_PRIx32_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_PRIx64),  PRIx64)  == 0,
        "helper_PRIx64_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_PRIdMAX), PRIdMAX) == 0,
        "helper_PRIdMAX_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_PRIuMAX), PRIuMAX) == 0,
        "helper_PRIuMAX_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_PRIdPTR), PRIdPTR) == 0,
        "helper_PRIdPTR_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_PRIuPTR), PRIuPTR) == 0,
        "helper_PRIuPTR_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_SCNd32),  SCNd32)  == 0,
        "helper_SCNd32_matches");
  CHECK(strcmp(clib_inttypes_fmt(CLIB_INTTYPES_SEL_SCNu64),  SCNu64)  == 0,
        "helper_SCNu64_matches");

  /* Out-of-range selector returns NULL (defensive). */
  CHECK(clib_inttypes_fmt(CLIB_INTTYPES_SEL_COUNT) == NULL,
        "helper_oob_returns_null");
  CHECK(clib_inttypes_fmt(-1) == NULL, "helper_negative_returns_null");
}

/* ---- 7. imaxabs / imaxdiv (C11 §7.8.2.1 / §7.8.2.2) ------------------ */
static void check_imaxabs_imaxdiv(void) {
  /* imaxabs positive / zero / negative. */
  CHECK(imaxabs((intmax_t)0)     == (intmax_t)0,    "imaxabs_zero");
  CHECK(imaxabs((intmax_t)7)     == (intmax_t)7,    "imaxabs_pos");
  CHECK(imaxabs((intmax_t)-7)    == (intmax_t)7,    "imaxabs_neg");
  CHECK(imaxabs(INTMAX_MAX)      == INTMAX_MAX,     "imaxabs_max");
  /* INTMAX_MIN is the documented UB-adjacent input; the contract is
   * "return input unchanged", same as abs/labs. */
  CHECK(imaxabs(INTMAX_MIN)      == INTMAX_MIN,     "imaxabs_min_carveout");

  /* imaxdiv: positive / negative numerator + denom, both signs of rem. */
  imaxdiv_t d;

  d = imaxdiv((intmax_t)17, (intmax_t)5);
  CHECK(d.quot ==  3 && d.rem ==  2, "imaxdiv_pos_pos");

  d = imaxdiv((intmax_t)-17, (intmax_t)5);
  CHECK(d.quot == -3 && d.rem == -2, "imaxdiv_neg_pos_rem_sign_of_numer");

  d = imaxdiv((intmax_t)17, (intmax_t)-5);
  CHECK(d.quot == -3 && d.rem ==  2, "imaxdiv_pos_neg");

  d = imaxdiv((intmax_t)-17, (intmax_t)-5);
  CHECK(d.quot ==  3 && d.rem == -2, "imaxdiv_neg_neg_rem_sign_of_numer");

  /* Exact division — rem == 0. */
  d = imaxdiv((intmax_t)20, (intmax_t)4);
  CHECK(d.quot ==  5 && d.rem ==  0, "imaxdiv_exact");

  /* Identity: numer == quot*denom + rem. */
  d = imaxdiv((intmax_t)1234567, (intmax_t)89);
  CHECK(d.quot * (intmax_t)89 + d.rem == (intmax_t)1234567,
        "imaxdiv_identity");

  /* Deny-clean divide-by-zero: returns sentinel, does not trap. */
  d = imaxdiv((intmax_t)42, (intmax_t)0);
  CHECK(d.quot == INTMAX_MAX && d.rem == 0, "imaxdiv_div0_pos_sentinel");
  d = imaxdiv((intmax_t)-42, (intmax_t)0);
  CHECK(d.quot == INTMAX_MIN && d.rem == 0, "imaxdiv_div0_neg_sentinel");
}

/* ---- 8. strtoimax / strtoumax (C11 §7.8.2.3 / §7.8.2.4) -------------- */
static void check_strto_imax_umax(void) {
  char *end = NULL;

  /* Basic decimal parse, *endptr advancement. */
  errno = 0;
  CHECK(strtoimax("  -1234rest", &end, 10) == (intmax_t)-1234,
        "strtoimax_basic_neg");
  CHECK(end != NULL && strcmp(end, "rest") == 0, "strtoimax_endptr_advanced");
  CHECK(errno == 0, "strtoimax_no_errno_on_success");

  /* Hex auto-detect via base=0. */
  errno = 0;
  CHECK(strtoimax("0x7f", &end, 0) == (intmax_t)0x7f, "strtoimax_base0_hex");
  CHECK(end != NULL && *end == '\0', "strtoimax_base0_endptr");

  /* Octal auto-detect via base=0. */
  errno = 0;
  CHECK(strtoimax("017", &end, 0) == (intmax_t)017, "strtoimax_base0_octal");

  /* Overflow clamp + ERANGE. */
  errno = 0;
  CHECK(strtoimax("99999999999999999999999999999999", &end, 10) == INTMAX_MAX,
        "strtoimax_overflow_clamps_to_max");
  CHECK(errno == ERANGE, "strtoimax_sets_erange_on_overflow");

  errno = 0;
  CHECK(strtoimax("-99999999999999999999999999999999", &end, 10) == INTMAX_MIN,
        "strtoimax_underflow_clamps_to_min");
  CHECK(errno == ERANGE, "strtoimax_sets_erange_on_underflow");

  /* strtoumax: positive parse + clamp. */
  errno = 0;
  CHECK(strtoumax("4242", &end, 10) == (uintmax_t)4242, "strtoumax_basic");

  errno = 0;
  CHECK(strtoumax("99999999999999999999999999999999", &end, 10) == UINTMAX_MAX,
        "strtoumax_overflow_clamps_to_max");
  CHECK(errno == ERANGE, "strtoumax_sets_erange_on_overflow");

  /* Invalid base: errno=EINVAL, return 0, endptr = nptr. */
  errno = 0;
  const char *bad = "42";
  CHECK(strtoimax(bad, &end, 1) == 0, "strtoimax_invalid_base_returns_zero");
  CHECK(errno == EINVAL, "strtoimax_sets_einval_on_bad_base");
  CHECK(end == bad, "strtoimax_endptr_unmoved_on_bad_base");

  errno = 0;
  CHECK(strtoumax(bad, &end, 37) == 0, "strtoumax_invalid_base_returns_zero");
  CHECK(errno == EINVAL, "strtoumax_sets_einval_on_bad_base");

  /* Round-trip with PRIdMAX / PRIuMAX formatting (slice 11 surface). */
  char buf[64];
  intmax_t  iv = (intmax_t)-1234567890;
  uintmax_t uv = (uintmax_t)1234567890u;
  int n;

  n = snprintf(buf, sizeof(buf), "%" PRIdMAX, iv);
  CHECK(n > 0 && (size_t)n < sizeof(buf), "strtoimax_roundtrip_snprintf_ok");
  errno = 0;
  CHECK(strtoimax(buf, &end, 10) == iv, "strtoimax_roundtrip_value");

  n = snprintf(buf, sizeof(buf), "%" PRIuMAX, uv);
  CHECK(n > 0 && (size_t)n < sizeof(buf), "strtoumax_roundtrip_snprintf_ok");
  errno = 0;
  CHECK(strtoumax(buf, &end, 10) == uv, "strtoumax_roundtrip_value");
}

int main(void) {
  printf("TEST:START:clib_inttypes\n");

  check_macro_shape();
  if (g_fail == 0) printf("TEST:PASS:clib_inttypes:macro_shape_pinned\n");

  check_printf_roundtrip();
  if (g_fail == 0) printf("TEST:PASS:clib_inttypes:printf_roundtrip_pinned\n");

  check_least_fast_format();
  if (g_fail == 0) printf("TEST:PASS:clib_inttypes:least_fast_format_pinned\n");

  check_max_ptr_roundtrip();
  if (g_fail == 0) printf("TEST:PASS:clib_inttypes:max_ptr_roundtrip_pinned\n");

  check_symbol_set_pinned();
  if (g_fail == 0) printf("TEST:PASS:clib_inttypes:symbol_set_pinned\n");

  check_imaxabs_imaxdiv();
  if (g_fail == 0) printf("TEST:PASS:clib_inttypes:imaxabs_imaxdiv_pinned\n");

  check_strto_imax_umax();
  if (g_fail == 0) printf("TEST:PASS:clib_inttypes:strto_imax_umax_pinned\n");

  if (g_fail != 0) {
    fprintf(stderr, "TEST:FAIL:clib_inttypes\n");
    return 1;
  }
  printf("TEST:PASS:clib_inttypes\n");
  return 0;
}
