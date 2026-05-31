/*
 * tests/clib_stdlib_test.c
 * Host unit test for the freestanding stdlib subset
 * (M7-TOOLCHAIN-004 slice 4, issue #407).
 *
 * Build with: build/scripts/test_clib_stdlib.sh
 *
 * Compiled with -fno-builtin so the assertions exercise OUR atoi /
 * strtol / strtoul / abs / labs rather than the host libc / compiler
 * builtins. Marker discipline matches slices 1/2/3: one
 * TEST:PASS:clib_stdlib:<sub> per scenario plus a final TEST:PASS:clib_stdlib.
 */

#include "../user/libs/clib/include/clib/stdlib.h"

/*
 * Pull in the freestanding <errno.h> nucleus so the errno-on-overflow
 * follow-up to PR #430 can be asserted directly against OUR `errno`
 * global and macro family rather than glibc's. The header guards on
 * SECUREOS_USER_LIBS_CLIB_ERRNO_H and the macro values match musl /
 * Linux numbering, so transitive inclusion of glibc's <errno.h>
 * (which does NOT happen via <stdio.h> on Debian bookworm — same
 * property the slice-5 test relies on) would still agree on numbering.
 */
#include "../user/libs/clib/include/clib/errno.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int g_fails = 0;

#define EXPECT_EQ_L(actual, expected, sub)                                   \
  do {                                                                       \
    long _a = (long)(actual);                                                \
    long _e = (long)(expected);                                              \
    if (_a != _e) {                                                          \
      printf("TEST:FAIL:clib_stdlib:%s: got %ld, expected %ld (%s:%d)\n",    \
             (sub), _a, _e, __FILE__, __LINE__);                             \
      g_fails++;                                                             \
    }                                                                        \
  } while (0)

#define EXPECT_EQ_UL(actual, expected, sub)                                  \
  do {                                                                       \
    unsigned long _a = (unsigned long)(actual);                              \
    unsigned long _e = (unsigned long)(expected);                            \
    if (_a != _e) {                                                          \
      printf("TEST:FAIL:clib_stdlib:%s: got %lu, expected %lu (%s:%d)\n",    \
             (sub), _a, _e, __FILE__, __LINE__);                             \
      g_fails++;                                                             \
    }                                                                        \
  } while (0)

#define EXPECT_EQ_LL(actual, expected, sub)                                  \
  do {                                                                       \
    long long _a = (long long)(actual);                                      \
    long long _e = (long long)(expected);                                    \
    if (_a != _e) {                                                          \
      printf("TEST:FAIL:clib_stdlib:%s: got %lld, expected %lld (%s:%d)\n", \
             (sub), _a, _e, __FILE__, __LINE__);                             \
      g_fails++;                                                             \
    }                                                                        \
  } while (0)

#define EXPECT_EQ_ULL(actual, expected, sub)                                 \
  do {                                                                       \
    unsigned long long _a = (unsigned long long)(actual);                    \
    unsigned long long _e = (unsigned long long)(expected);                  \
    if (_a != _e) {                                                          \
      printf("TEST:FAIL:clib_stdlib:%s: got %llu, expected %llu (%s:%d)\n", \
             (sub), _a, _e, __FILE__, __LINE__);                             \
      g_fails++;                                                             \
    }                                                                        \
  } while (0)

#define EXPECT_EQ_PTR(actual, expected, sub)                                 \
  do {                                                                       \
    const void *_a = (const void *)(actual);                                 \
    const void *_e = (const void *)(expected);                               \
    if (_a != _e) {                                                          \
      printf("TEST:FAIL:clib_stdlib:%s: ptr mismatch (%s:%d)\n", (sub),      \
             __FILE__, __LINE__);                                            \
      g_fails++;                                                             \
    }                                                                        \
  } while (0)

#define PASS(sub) printf("TEST:PASS:clib_stdlib:%s\n", (sub))

/* ---------- atoi -------------------------------------------------------- */

static void test_atoi_basic(void) {
  EXPECT_EQ_L(atoi("0"),          0,  "atoi_basic");
  EXPECT_EQ_L(atoi("1"),          1,  "atoi_basic");
  EXPECT_EQ_L(atoi("42"),         42, "atoi_basic");
  EXPECT_EQ_L(atoi("-7"),         -7, "atoi_basic");
  EXPECT_EQ_L(atoi("+9"),         9,  "atoi_basic");
  EXPECT_EQ_L(atoi(""),           0,  "atoi_basic");
  EXPECT_EQ_L(atoi("abc"),        0,  "atoi_basic");
  EXPECT_EQ_L(atoi("   123"),     123,"atoi_basic");
  EXPECT_EQ_L(atoi("12abc"),      12, "atoi_basic");
  EXPECT_EQ_L(atoi("\t\n -50"),   -50,"atoi_basic");
  PASS("atoi_basic");
}

/* ---------- strtol ------------------------------------------------------ */

static void test_strtol_decimal(void) {
  char *end;
  EXPECT_EQ_L(strtol("0", &end, 10),   0L, "strtol_decimal");
  EXPECT_EQ_L(strtol("123", &end, 10), 123L, "strtol_decimal");
  EXPECT_EQ_L(*end, '\0',              "strtol_decimal");
  EXPECT_EQ_L(strtol("-456", &end, 10),-456L, "strtol_decimal");
  EXPECT_EQ_L(strtol("  +7x", &end, 10), 7L, "strtol_decimal");
  EXPECT_EQ_L(*end, 'x',                  "strtol_decimal");
  PASS("strtol_decimal");
}

static void test_strtol_hex_octal_auto(void) {
  char *end;
  /* Explicit base 16, prefix optional */
  EXPECT_EQ_L(strtol("ff",   &end, 16), 0xffL,   "strtol_hex_octal_auto");
  EXPECT_EQ_L(strtol("0xFF", &end, 16), 0xffL,   "strtol_hex_octal_auto");
  EXPECT_EQ_L(strtol("-0x10",&end, 16), -16L,    "strtol_hex_octal_auto");
  /* base=0 detection */
  EXPECT_EQ_L(strtol("0x2a", &end, 0),  42L,     "strtol_hex_octal_auto");
  EXPECT_EQ_L(strtol("052",  &end, 0),  052L,    "strtol_hex_octal_auto");
  EXPECT_EQ_L(strtol("99",   &end, 0),  99L,     "strtol_hex_octal_auto");
  EXPECT_EQ_L(strtol("0",    &end, 0),  0L,      "strtol_hex_octal_auto");
  /* base=2 */
  EXPECT_EQ_L(strtol("1011", &end, 2),  11L,     "strtol_hex_octal_auto");
  /* base=36 */
  EXPECT_EQ_L(strtol("z",    &end, 36), 35L,     "strtol_hex_octal_auto");
  PASS("strtol_hex_octal_auto");
}

static void test_strtol_endptr_no_digits(void) {
  const char *src = "  abc";
  char       *end = NULL;
  long        v   = strtol(src, &end, 10);
  EXPECT_EQ_L(v, 0L, "strtol_endptr_no_digits");
  /* Per C99: if no conversion can be performed, *endptr is set to nptr
   * (the original pointer), not the post-whitespace pointer. */
  EXPECT_EQ_PTR(end, src, "strtol_endptr_no_digits");
  PASS("strtol_endptr_no_digits");
}

static void test_strtol_overflow_clamp(void) {
  char *end;
  /* Build a string guaranteed to overflow LONG_MAX regardless of size. */
  EXPECT_EQ_L(strtol("99999999999999999999999999999999", &end, 10),
              LONG_MAX, "strtol_overflow_clamp");
  EXPECT_EQ_L(strtol("-99999999999999999999999999999999", &end, 10),
              LONG_MIN, "strtol_overflow_clamp");
  /* LONG_MIN itself must round-trip through the signed-clamp path. */
  char buf[64];
  /* Build "-<|LONG_MIN|>" portably (snprintf treats LONG_MIN safely). */
  snprintf(buf, sizeof buf, "%ld", LONG_MIN);
  EXPECT_EQ_L(strtol(buf, &end, 10), LONG_MIN, "strtol_overflow_clamp");
  EXPECT_EQ_L(*end, '\0',                        "strtol_overflow_clamp");
  /* LONG_MAX itself must NOT clamp. */
  snprintf(buf, sizeof buf, "%ld", LONG_MAX);
  EXPECT_EQ_L(strtol(buf, &end, 10), LONG_MAX, "strtol_overflow_clamp");
  PASS("strtol_overflow_clamp");
}

/* ---------- errno-on-overflow follow-up to slice 5 (PR #430) ----------- *
 *
 * The slice-5 errno nucleus shipped the `int errno` global + the
 * ERANGE / EINVAL macros precisely so the stdlib clamp paths could
 * flip from "silent" to canonical ISO C / POSIX `errno = ERANGE`.
 * These sub-assertions pin that contract:
 *
 *   - Overflow on any of strtol/strtoul/strtoll/strtoull sets
 *     errno to ERANGE (in addition to clamping the return value).
 *   - A bad `base` argument sets errno to EINVAL (POSIX extension;
 *     musl matches).
 *   - A successful parse does NOT clobber a caller-preset errno.
 */
static void test_errno_overflow_set_erange(void) {
  char *end;
  errno = 0;
  (void)strtol("99999999999999999999999999999999", &end, 10);
  EXPECT_EQ_L(errno, ERANGE, "errno_overflow_set_erange");

  errno = 0;
  (void)strtol("-99999999999999999999999999999999", &end, 10);
  EXPECT_EQ_L(errno, ERANGE, "errno_overflow_set_erange");

  errno = 0;
  (void)strtoul("99999999999999999999999999999999", &end, 10);
  EXPECT_EQ_L(errno, ERANGE, "errno_overflow_set_erange");

  errno = 0;
  (void)strtoll("99999999999999999999999999999999", &end, 10);
  EXPECT_EQ_L(errno, ERANGE, "errno_overflow_set_erange");

  errno = 0;
  (void)strtoll("-99999999999999999999999999999999", &end, 10);
  EXPECT_EQ_L(errno, ERANGE, "errno_overflow_set_erange");

  errno = 0;
  (void)strtoull("99999999999999999999999999999999", &end, 10);
  EXPECT_EQ_L(errno, ERANGE, "errno_overflow_set_erange");
  PASS("errno_overflow_set_erange");
}

static void test_errno_invalid_base_set_einval(void) {
  char *end;
  errno = 0;
  (void)strtol("10", &end, 1);
  EXPECT_EQ_L(errno, EINVAL, "errno_invalid_base_set_einval");

  errno = 0;
  (void)strtol("10", &end, 37);
  EXPECT_EQ_L(errno, EINVAL, "errno_invalid_base_set_einval");

  errno = 0;
  (void)strtoul("10", &end, 1);
  EXPECT_EQ_L(errno, EINVAL, "errno_invalid_base_set_einval");

  errno = 0;
  (void)strtoll("10", &end, 1);
  EXPECT_EQ_L(errno, EINVAL, "errno_invalid_base_set_einval");

  errno = 0;
  (void)strtoull("10", &end, 37);
  EXPECT_EQ_L(errno, EINVAL, "errno_invalid_base_set_einval");
  PASS("errno_invalid_base_set_einval");
}

static void test_errno_success_preserved(void) {
  /*
   * Canonical contract: errno is set on error, left alone on success.
   * Preset errno to a sentinel (EBADF) and confirm a clean parse does
   * not clobber it. Also confirms a "no digits" call (which returns 0
   * but is not an overflow) does not clobber errno — same musl posture.
   */
  char *end;
  errno = EBADF;
  long v = strtol("  +42x", &end, 10);
  EXPECT_EQ_L(v, 42L,             "errno_success_preserved");
  EXPECT_EQ_L(errno, EBADF,       "errno_success_preserved");

  errno = EBADF;
  (void)strtoul("0xff", &end, 0);
  EXPECT_EQ_L(errno, EBADF,       "errno_success_preserved");

  errno = EBADF;
  (void)strtoll("8589934592", &end, 10);
  EXPECT_EQ_L(errno, EBADF,       "errno_success_preserved");

  errno = EBADF;
  (void)strtoull("42", &end, 10);
  EXPECT_EQ_L(errno, EBADF,       "errno_success_preserved");

  /* No-digits path: returns 0, leaves errno untouched. */
  errno = EBADF;
  long w = strtol("  abc", &end, 10);
  EXPECT_EQ_L(w, 0L,              "errno_success_preserved");
  EXPECT_EQ_L(errno, EBADF,       "errno_success_preserved");

  /* LONG_MAX exactly: not an overflow, must not set errno. */
  char buf[64];
  snprintf(buf, sizeof buf, "%ld", LONG_MAX);
  errno = EBADF;
  (void)strtol(buf, &end, 10);
  EXPECT_EQ_L(errno, EBADF,       "errno_success_preserved");
  PASS("errno_success_preserved");
}

static void test_strtol_invalid_base(void) {
  const char *src = "10";
  char       *end = NULL;
  EXPECT_EQ_L(strtol(src, &end, 1),  0L,    "strtol_invalid_base");
  EXPECT_EQ_PTR(end, src,                    "strtol_invalid_base");
  EXPECT_EQ_L(strtol(src, &end, 37), 0L,    "strtol_invalid_base");
  EXPECT_EQ_PTR(end, src,                    "strtol_invalid_base");
  PASS("strtol_invalid_base");
}

/* ---------- strtoul ----------------------------------------------------- */

static void test_strtoul_basic(void) {
  char *end;
  EXPECT_EQ_UL(strtoul("0",   &end, 10), 0UL,   "strtoul_basic");
  EXPECT_EQ_UL(strtoul("42",  &end, 10), 42UL,  "strtoul_basic");
  EXPECT_EQ_UL(strtoul("0xff",&end, 0),  255UL, "strtoul_basic");
  /* Negative parse: result is negation mod (ULONG_MAX+1). */
  EXPECT_EQ_UL(strtoul("-1",  &end, 10), ULONG_MAX, "strtoul_basic");
  EXPECT_EQ_UL(strtoul("-2",  &end, 10), ULONG_MAX - 1UL, "strtoul_basic");
  PASS("strtoul_basic");
}

static void test_strtoul_overflow_clamp(void) {
  char *end;
  EXPECT_EQ_UL(strtoul("99999999999999999999999999999999", &end, 10),
               ULONG_MAX, "strtoul_overflow_clamp");
  /* ULONG_MAX itself must NOT clamp. */
  char buf[64];
  snprintf(buf, sizeof buf, "%lu", ULONG_MAX);
  EXPECT_EQ_UL(strtoul(buf, &end, 10), ULONG_MAX, "strtoul_overflow_clamp");
  EXPECT_EQ_L(*end, '\0',                          "strtoul_overflow_clamp");
  PASS("strtoul_overflow_clamp");
}

/* ---------- strtoll / strtoull ----------------------------------------- */

static void test_strtoll_decimal(void) {
  char *end;
  EXPECT_EQ_LL(strtoll("0",   &end, 10),  0LL,   "strtoll_decimal");
  EXPECT_EQ_LL(strtoll("123", &end, 10),  123LL, "strtoll_decimal");
  EXPECT_EQ_L (*end, '\0',                       "strtoll_decimal");
  EXPECT_EQ_LL(strtoll("-456",&end, 10), -456LL, "strtoll_decimal");
  /* base=0 hex auto-detect via the shared prefix path. */
  EXPECT_EQ_LL(strtoll("0x7fffffff", &end, 0),
               0x7fffffffLL, "strtoll_decimal");
  /* Width that LONG might not cover on a 32-bit host: 1<<33 = 8589934592. */
  EXPECT_EQ_LL(strtoll("8589934592", &end, 10),
               8589934592LL, "strtoll_decimal");
  EXPECT_EQ_LL(strtoll("-8589934592", &end, 10),
               -8589934592LL, "strtoll_decimal");
  PASS("strtoll_decimal");
}

static void test_strtoll_overflow_clamp(void) {
  char *end;
  EXPECT_EQ_LL(strtoll("99999999999999999999999999999999", &end, 10),
               LLONG_MAX, "strtoll_overflow_clamp");
  EXPECT_EQ_LL(strtoll("-99999999999999999999999999999999", &end, 10),
               LLONG_MIN, "strtoll_overflow_clamp");
  /* LLONG_MIN itself must round-trip through the signed-clamp path. */
  char buf[64];
  snprintf(buf, sizeof buf, "%lld", LLONG_MIN);
  EXPECT_EQ_LL(strtoll(buf, &end, 10), LLONG_MIN, "strtoll_overflow_clamp");
  EXPECT_EQ_L (*end, '\0',                        "strtoll_overflow_clamp");
  /* LLONG_MAX itself must NOT clamp. */
  snprintf(buf, sizeof buf, "%lld", LLONG_MAX);
  EXPECT_EQ_LL(strtoll(buf, &end, 10), LLONG_MAX, "strtoll_overflow_clamp");
  PASS("strtoll_overflow_clamp");
}

static void test_strtoull_basic(void) {
  char *end;
  EXPECT_EQ_ULL(strtoull("0",    &end, 10),  0ULL,   "strtoull_basic");
  EXPECT_EQ_ULL(strtoull("42",   &end, 10),  42ULL,  "strtoull_basic");
  EXPECT_EQ_ULL(strtoull("0xff", &end, 0),   255ULL, "strtoull_basic");
  /* Negative parse: result is negation mod (ULLONG_MAX+1). */
  EXPECT_EQ_ULL(strtoull("-1",   &end, 10),  ULLONG_MAX,        "strtoull_basic");
  EXPECT_EQ_ULL(strtoull("-2",   &end, 10),  ULLONG_MAX - 1ULL, "strtoull_basic");
  /* Width that ULONG might not cover on a 32-bit host. */
  EXPECT_EQ_ULL(strtoull("18446744073709551614", &end, 10),
                ULLONG_MAX - 1ULL, "strtoull_basic");
  PASS("strtoull_basic");
}

static void test_strtoull_overflow_clamp(void) {
  char *end;
  EXPECT_EQ_ULL(strtoull("99999999999999999999999999999999", &end, 10),
                ULLONG_MAX, "strtoull_overflow_clamp");
  /* ULLONG_MAX itself must NOT clamp. */
  char buf[64];
  snprintf(buf, sizeof buf, "%llu", ULLONG_MAX);
  EXPECT_EQ_ULL(strtoull(buf, &end, 10), ULLONG_MAX,
                "strtoull_overflow_clamp");
  EXPECT_EQ_L (*end, '\0',                "strtoull_overflow_clamp");
  PASS("strtoull_overflow_clamp");
}

static void test_strtoll_endptr_no_digits(void) {
  const char *src = "  abc";
  char       *end = NULL;
  long long   v   = strtoll(src, &end, 10);
  EXPECT_EQ_LL(v, 0LL,                        "strtoll_endptr_no_digits");
  /* Per C99: if no conversion can be performed, *endptr is set to nptr. */
  EXPECT_EQ_PTR(end, src,                     "strtoll_endptr_no_digits");
  /* Same shape on strtoull. */
  end = NULL;
  unsigned long long uv = strtoull(src, &end, 10);
  EXPECT_EQ_ULL(uv, 0ULL,                     "strtoll_endptr_no_digits");
  EXPECT_EQ_PTR(end, src,                     "strtoll_endptr_no_digits");
  PASS("strtoll_endptr_no_digits");
}

static void test_strtoll_invalid_base(void) {
  const char *src = "10";
  char       *end = NULL;
  EXPECT_EQ_LL(strtoll(src, &end, 1),  0LL,    "strtoll_invalid_base");
  EXPECT_EQ_PTR(end, src,                       "strtoll_invalid_base");
  EXPECT_EQ_LL(strtoll(src, &end, 37), 0LL,    "strtoll_invalid_base");
  EXPECT_EQ_PTR(end, src,                       "strtoll_invalid_base");
  end = NULL;
  EXPECT_EQ_ULL(strtoull(src, &end, 1),  0ULL, "strtoll_invalid_base");
  EXPECT_EQ_PTR(end, src,                       "strtoll_invalid_base");
  EXPECT_EQ_ULL(strtoull(src, &end, 37), 0ULL, "strtoll_invalid_base");
  EXPECT_EQ_PTR(end, src,                       "strtoll_invalid_base");
  PASS("strtoll_invalid_base");
}

/* ---------- abs / labs -------------------------------------------------- */

static void test_abs_labs(void) {
  EXPECT_EQ_L(abs(0),   0,   "abs_labs");
  EXPECT_EQ_L(abs(7),   7,   "abs_labs");
  EXPECT_EQ_L(abs(-7),  7,   "abs_labs");
  EXPECT_EQ_L(abs(INT_MAX), INT_MAX, "abs_labs");
  /* INT_MIN: undefined-by-standard; our contract returns unchanged. */
  EXPECT_EQ_L(abs(INT_MIN), INT_MIN, "abs_labs");

  EXPECT_EQ_L(labs(0L),    0L,    "abs_labs");
  EXPECT_EQ_L(labs(123L),  123L,  "abs_labs");
  EXPECT_EQ_L(labs(-123L), 123L,  "abs_labs");
  EXPECT_EQ_L(labs(LONG_MAX), LONG_MAX, "abs_labs");
  EXPECT_EQ_L(labs(LONG_MIN), LONG_MIN, "abs_labs");
  PASS("abs_labs");
}

/* ---------- symbol_set_pinned ------------------------------------------ *
 *
 * Take the address of every shipped symbol so a link-time rename or a
 * silent drop will flip the build (or trip our nonzero-address check at
 * runtime). Mirrors the discipline used by slices 1/2/3.
 */
static void test_symbol_set_pinned(void) {
  void *syms[] = {
    (void *)atoi,
    (void *)strtol,
    (void *)strtoul,
    (void *)strtoll,
    (void *)strtoull,
    (void *)abs,
    (void *)labs,
  };
  for (size_t i = 0; i < sizeof syms / sizeof syms[0]; i++) {
    if (syms[i] == NULL) {
      printf("TEST:FAIL:clib_stdlib:symbol_set_pinned: null symbol at %zu\n",
             i);
      g_fails++;
    }
  }
  /* Pin the count: any future slice that adds/removes a symbol MUST
   * update this number and the symbol_set list above in lockstep, the
   * same way slices 1/2/3 pin their families. */
  if (sizeof syms / sizeof syms[0] != 7) {
    printf("TEST:FAIL:clib_stdlib:symbol_set_pinned: expected 7 symbols\n");
    g_fails++;
  }
  PASS("symbol_set_pinned");
}

int main(void) {
  printf("TEST:START:clib_stdlib\n");

  test_atoi_basic();
  test_strtol_decimal();
  test_strtol_hex_octal_auto();
  test_strtol_endptr_no_digits();
  test_strtol_overflow_clamp();
  test_strtol_invalid_base();
  test_strtoul_basic();
  test_strtoul_overflow_clamp();
  test_strtoll_decimal();
  test_strtoll_overflow_clamp();
  test_strtoll_endptr_no_digits();
  test_strtoll_invalid_base();
  test_strtoull_basic();
  test_strtoull_overflow_clamp();
  test_errno_overflow_set_erange();
  test_errno_invalid_base_set_einval();
  test_errno_success_preserved();
  test_abs_labs();
  test_symbol_set_pinned();

  if (g_fails != 0) {
    printf("TEST:FAIL:clib_stdlib: %d sub-assertion(s) failed\n", g_fails);
    return 1;
  }
  printf("TEST:PASS:clib_stdlib\n");
  return 0;
}
