/**
 * @file tests/clib_string_test.c
 * @brief Host unit test for the freestanding string / memory family
 *        (M7-TOOLCHAIN-004 slice 1, issue #407).
 *
 * Test markers (consumed by build/scripts/test_clib_string.sh):
 *   TEST:PASS:clib_string:memcpy_basic
 *   TEST:PASS:clib_string:memmove_overlap_forward
 *   TEST:PASS:clib_string:memmove_overlap_backward
 *   TEST:PASS:clib_string:memset_fill
 *   TEST:PASS:clib_string:memcmp_order
 *   TEST:PASS:clib_string:memchr_hit_and_miss
 *   TEST:PASS:clib_string:strlen_and_strnlen
 *   TEST:PASS:clib_string:strcmp_order
 *   TEST:PASS:clib_string:strncmp_bounded
 *   TEST:PASS:clib_string:strcpy_and_strncpy_pad
 *   TEST:PASS:clib_string:strcat_and_strncat
 *   TEST:PASS:clib_string:strchr_and_strrchr
 *   TEST:PASS:clib_string:strstr_hit_and_miss
 *   TEST:PASS:clib_string:strspn_basic
 *   TEST:PASS:clib_string:strcspn_basic
 *   TEST:PASS:clib_string:strpbrk_hit_and_miss
 *   TEST:PASS:clib_string:strtok_walks_tokens
 *   TEST:PASS:clib_string:strtok_r_reentrant_independence
 *   TEST:PASS:clib_string:symbol_set_pinned
 *   TEST:PASS:clib_string
 *
 * The driver script compiles the unit-under-test with `-fno-builtin` so
 * `memcpy` etc. are NOT folded to host-libc builtins; the assertions
 * exercise the implementations shipped in `user/libs/clib/src/string.c`.
 */

#include "../user/libs/clib/include/clib/string.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int g_failures = 0;

#define CHECK(cond, label)                                                     \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("TEST:FAIL:clib_string:" label "\n");                             \
      g_failures++;                                                            \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define PASS(label) printf("TEST:PASS:clib_string:" label "\n")

/* --- memory family ------------------------------------------------------ */

static void test_memcpy_basic(void) {
  const char *src = "abcdef";
  char        dst[7] = {0};
  void       *ret    = memcpy(dst, src, 6);
  CHECK(ret == dst, "memcpy_basic");
  CHECK(dst[0] == 'a' && dst[5] == 'f' && dst[6] == 0, "memcpy_basic");
  PASS("memcpy_basic");
}

static void test_memmove_overlap_forward(void) {
  /* Forward overlap: dst > src in the same buffer. Naive memcpy would
   * stomp on src; memmove must copy backwards. */
  char buf[16] = "0123456789ABCDE";
  void *ret = memmove(buf + 2, buf, 10); /* "0123456789" -> buf[2..11] */
  CHECK(ret == buf + 2, "memmove_overlap_forward");
  /* Result: "01" + "0123456789" + "CDE" + '\0' */
  CHECK(buf[0]  == '0' && buf[1]  == '1', "memmove_overlap_forward");
  CHECK(buf[2]  == '0' && buf[11] == '9', "memmove_overlap_forward");
  CHECK(buf[12] == 'C' && buf[14] == 'E' && buf[15] == 0,
        "memmove_overlap_forward");
  PASS("memmove_overlap_forward");
}

static void test_memmove_overlap_backward(void) {
  /* Backward overlap: dst < src in the same buffer. Forward copy is
   * safe here; memmove must NOT corrupt the trailing bytes. */
  char buf[16] = "0123456789ABCDE";
  void *ret = memmove(buf, buf + 2, 10); /* "23456789AB" -> buf[0..9] */
  CHECK(ret == buf, "memmove_overlap_backward");
  CHECK(buf[0] == '2' && buf[9] == 'B', "memmove_overlap_backward");
  /* Tail unchanged from "ABCDE" but buf[8..9] now hold 'A','B'; buf[10..14]
   * are whatever memmove left — must still be the original "ABCDE" tail
   * after the first 10-byte copy lands. */
  CHECK(buf[10] == 'A' && buf[14] == 'E' && buf[15] == 0,
        "memmove_overlap_backward");
  PASS("memmove_overlap_backward");
}

static void test_memset_fill(void) {
  unsigned char buf[8];
  memset(buf, 0xAB, sizeof(buf));
  for (size_t i = 0; i < sizeof(buf); i++) {
    CHECK(buf[i] == 0xAB, "memset_fill");
  }
  memset(buf, 0, 4);
  CHECK(buf[0] == 0 && buf[3] == 0 && buf[4] == 0xAB, "memset_fill");
  PASS("memset_fill");
}

static void test_memcmp_order(void) {
  CHECK(memcmp("abc", "abc", 3) == 0, "memcmp_order");
  CHECK(memcmp("abc", "abd", 3) < 0, "memcmp_order");
  CHECK(memcmp("abd", "abc", 3) > 0, "memcmp_order");
  /* unsigned-char ordering: 0x80 > 0x01 even if host char is signed */
  unsigned char hi[1] = {0x80};
  unsigned char lo[1] = {0x01};
  CHECK(memcmp(hi, lo, 1) > 0, "memcmp_order");
  PASS("memcmp_order");
}

static void test_memchr_hit_and_miss(void) {
  const char *s = "hello";
  CHECK(memchr(s, 'l', 5) == s + 2, "memchr_hit_and_miss");
  CHECK(memchr(s, 'z', 5) == NULL,  "memchr_hit_and_miss");
  /* Bounded miss: 'o' is at index 4, but we only allow 4 bytes. */
  CHECK(memchr(s, 'o', 4) == NULL,  "memchr_hit_and_miss");
  PASS("memchr_hit_and_miss");
}

/* --- string family ------------------------------------------------------ */

static void test_strlen_and_strnlen(void) {
  CHECK(strlen("") == 0, "strlen_and_strnlen");
  CHECK(strlen("hello") == 5, "strlen_and_strnlen");
  CHECK(strnlen("hello", 3) == 3, "strlen_and_strnlen");
  CHECK(strnlen("hi", 8) == 2, "strlen_and_strnlen");
  /* no-NUL buffer must stop at max */
  char buf[4] = {'a', 'b', 'c', 'd'};
  CHECK(strnlen(buf, 4) == 4, "strlen_and_strnlen");
  PASS("strlen_and_strnlen");
}

static void test_strcmp_order(void) {
  CHECK(strcmp("abc", "abc") == 0, "strcmp_order");
  CHECK(strcmp("abc", "abd") < 0,  "strcmp_order");
  CHECK(strcmp("abd", "abc") > 0,  "strcmp_order");
  CHECK(strcmp("abc", "abcd") < 0, "strcmp_order");
  PASS("strcmp_order");
}

static void test_strncmp_bounded(void) {
  CHECK(strncmp("abc", "abd", 2) == 0, "strncmp_bounded");
  CHECK(strncmp("abc", "abd", 3) < 0,  "strncmp_bounded");
  CHECK(strncmp("abc", "abcd", 5) < 0, "strncmp_bounded");
  CHECK(strncmp("", "", 4) == 0,       "strncmp_bounded");
  PASS("strncmp_bounded");
}

static void test_strcpy_and_strncpy_pad(void) {
  char dst[8];
  memset(dst, '?', sizeof(dst));
  CHECK(strcpy(dst, "abc") == dst, "strcpy_and_strncpy_pad");
  CHECK(dst[0] == 'a' && dst[2] == 'c' && dst[3] == 0,
        "strcpy_and_strncpy_pad");
  /* historical strncpy: zero-pad up to n bytes */
  char pad[8];
  memset(pad, '?', sizeof(pad));
  strncpy(pad, "hi", 6);
  CHECK(pad[0] == 'h' && pad[1] == 'i', "strcpy_and_strncpy_pad");
  for (int i = 2; i < 6; i++) {
    CHECK(pad[i] == 0, "strcpy_and_strncpy_pad");
  }
  /* bytes past n are untouched */
  CHECK(pad[6] == '?' && pad[7] == '?', "strcpy_and_strncpy_pad");
  /* truncation: no NUL written when src len >= n */
  char trunc[4] = {'X', 'X', 'X', 'X'};
  strncpy(trunc, "abcd", 4);
  CHECK(trunc[0] == 'a' && trunc[3] == 'd', "strcpy_and_strncpy_pad");
  PASS("strcpy_and_strncpy_pad");
}

static void test_strcat_and_strncat(void) {
  char buf[16] = "hi";
  CHECK(strcat(buf, " there") == buf, "strcat_and_strncat");
  CHECK(strcmp(buf, "hi there") == 0, "strcat_and_strncat");
  /* strncat always NUL-terminates and copies at most n chars from src */
  char nbuf[16] = "foo";
  strncat(nbuf, "barbaz", 3);
  CHECK(strcmp(nbuf, "foobar") == 0, "strcat_and_strncat");
  PASS("strcat_and_strncat");
}

static void test_strchr_and_strrchr(void) {
  const char *s = "abcabc";
  CHECK(strchr(s, 'b') == s + 1,  "strchr_and_strrchr");
  CHECK(strrchr(s, 'b') == s + 4, "strchr_and_strrchr");
  CHECK(strchr(s, 'z') == NULL,   "strchr_and_strrchr");
  /* The NUL terminator is itself searchable. */
  CHECK(strchr(s, '\0') == s + 6, "strchr_and_strrchr");
  CHECK(strrchr(s, '\0') == s + 6, "strchr_and_strrchr");
  PASS("strchr_and_strrchr");
}

static void test_strstr_hit_and_miss(void) {
  const char *hay = "the quick brown fox";
  CHECK(strstr(hay, "quick") == hay + 4, "strstr_hit_and_miss");
  CHECK(strstr(hay, "")      == hay,     "strstr_hit_and_miss");
  CHECK(strstr(hay, "slow")  == NULL,    "strstr_hit_and_miss");
  /* needle longer than haystack must miss without OOB read */
  CHECK(strstr("ab", "abcdef") == NULL, "strstr_hit_and_miss");
  PASS("strstr_hit_and_miss");
}

/* --- tokenize / span family (slice 12) -------------------------------- */

static void test_strspn_basic(void) {
  /* Leading run of accept-class bytes. */
  CHECK(strspn("aabbcXY", "abc") == 5, "strspn_basic");
  /* Empty accept set -> 0. */
  CHECK(strspn("abc", "") == 0, "strspn_basic");
  /* All-accept -> full length. */
  CHECK(strspn("abc", "cba") == 3, "strspn_basic");
  /* First byte not in set -> 0. */
  CHECK(strspn("xabc", "abc") == 0, "strspn_basic");
  /* Empty input -> 0. */
  CHECK(strspn("", "abc") == 0, "strspn_basic");
  /* High-bit byte treated as unsigned (matches strcmp posture). */
  const char hi[2] = { (char)0x80, 0 };
  const char set[2] = { (char)0x80, 0 };
  CHECK(strspn(hi, set) == 1, "strspn_basic");
  PASS("strspn_basic");
}

static void test_strcspn_basic(void) {
  /* Leading run of bytes NOT in reject set. */
  CHECK(strcspn("hello/world", "/") == 5, "strcspn_basic");
  /* No hit -> full length. */
  CHECK(strcspn("hello", "XYZ") == 5, "strcspn_basic");
  /* First byte in reject -> 0. */
  CHECK(strcspn("/abc", "/") == 0, "strcspn_basic");
  /* Empty input -> 0. */
  CHECK(strcspn("", "/") == 0, "strcspn_basic");
  /* Empty reject -> full length. */
  CHECK(strcspn("abc", "") == 3, "strcspn_basic");
  PASS("strcspn_basic");
}

static void test_strpbrk_hit_and_miss(void) {
  const char *s = "hello world";
  /* First match is the space at index 5. */
  CHECK(strpbrk(s, " \t") == s + 5, "strpbrk_hit_and_miss");
  /* Multi-char accept set: first 'o' at index 4. */
  CHECK(strpbrk(s, "o") == s + 4, "strpbrk_hit_and_miss");
  /* No match -> NULL. */
  CHECK(strpbrk("abc", "XYZ") == NULL, "strpbrk_hit_and_miss");
  /* Empty input -> NULL. */
  CHECK(strpbrk("", "abc") == NULL, "strpbrk_hit_and_miss");
  /* Empty accept set -> NULL. */
  CHECK(strpbrk("abc", "") == NULL, "strpbrk_hit_and_miss");
  PASS("strpbrk_hit_and_miss");
}

static void test_strtok_walks_tokens(void) {
  /* Classic strtok walk: split a path-ish string on '/'. The first
   * call seeds with the buffer; subsequent calls pass NULL to resume
   * from the saved state. */
  char buf[] = "//usr//bin/cc//";
  char *t1 = strtok(buf, "/");
  CHECK(t1 != NULL && strcmp(t1, "usr") == 0, "strtok_walks_tokens");
  char *t2 = strtok(NULL, "/");
  CHECK(t2 != NULL && strcmp(t2, "bin") == 0, "strtok_walks_tokens");
  char *t3 = strtok(NULL, "/");
  CHECK(t3 != NULL && strcmp(t3, "cc") == 0, "strtok_walks_tokens");
  /* Trailing delimiters -> no more tokens -> NULL. */
  CHECK(strtok(NULL, "/") == NULL, "strtok_walks_tokens");
  /* All-delim input -> immediately NULL. */
  char only_delims[] = "////";
  CHECK(strtok(only_delims, "/") == NULL, "strtok_walks_tokens");
  /* Empty input -> NULL. */
  char empty[] = "";
  CHECK(strtok(empty, "/") == NULL, "strtok_walks_tokens");
  PASS("strtok_walks_tokens");
}

static void test_strtok_r_reentrant_independence(void) {
  /* Two interleaved walks must not share state. */
  char a[] = "one,two,three";
  char b[] = "alpha;beta;gamma";
  char *sa = NULL;
  char *sb = NULL;

  char *ta1 = strtok_r(a, ",", &sa);
  char *tb1 = strtok_r(b, ";", &sb);
  char *ta2 = strtok_r(NULL, ",", &sa);
  char *tb2 = strtok_r(NULL, ";", &sb);
  char *ta3 = strtok_r(NULL, ",", &sa);
  char *tb3 = strtok_r(NULL, ";", &sb);

  CHECK(ta1 && strcmp(ta1, "one")    == 0, "strtok_r_reentrant_independence");
  CHECK(ta2 && strcmp(ta2, "two")    == 0, "strtok_r_reentrant_independence");
  CHECK(ta3 && strcmp(ta3, "three")  == 0, "strtok_r_reentrant_independence");
  CHECK(tb1 && strcmp(tb1, "alpha")  == 0, "strtok_r_reentrant_independence");
  CHECK(tb2 && strcmp(tb2, "beta")   == 0, "strtok_r_reentrant_independence");
  CHECK(tb3 && strcmp(tb3, "gamma")  == 0, "strtok_r_reentrant_independence");

  CHECK(strtok_r(NULL, ",", &sa) == NULL, "strtok_r_reentrant_independence");
  CHECK(strtok_r(NULL, ";", &sb) == NULL, "strtok_r_reentrant_independence");

  /* Multi-character delimiter set: split on whitespace OR comma. */
  char mixed[] = " \tfoo, bar\tbaz ";
  char *sm = NULL;
  char *m1 = strtok_r(mixed, " \t,", &sm);
  char *m2 = strtok_r(NULL,  " \t,", &sm);
  char *m3 = strtok_r(NULL,  " \t,", &sm);
  CHECK(m1 && strcmp(m1, "foo") == 0, "strtok_r_reentrant_independence");
  CHECK(m2 && strcmp(m2, "bar") == 0, "strtok_r_reentrant_independence");
  CHECK(m3 && strcmp(m3, "baz") == 0, "strtok_r_reentrant_independence");
  CHECK(strtok_r(NULL, " \t,", &sm) == NULL,
        "strtok_r_reentrant_independence");

  /* Defensive: NULL saveptr -> NULL without crash. */
  CHECK(strtok_r(NULL, ",", NULL) == NULL, "strtok_r_reentrant_independence");

  PASS("strtok_r_reentrant_independence");
}

/* --- ABI pin ----------------------------------------------------------- */
/*
 * Drift test: pin the exact slice-1 symbol set so a TinyCC drop or an
 * unrelated PR cannot silently remove a member of the family. Every
 * declared symbol must be reachable through a function pointer; the
 * compiler will not let us take the address of a missing symbol.
 */

static void test_symbol_set_pinned(void) {
  void *(*p_memcpy) (void *, const void *, size_t)        = memcpy;
  void *(*p_memmove)(void *, const void *, size_t)        = memmove;
  void *(*p_memset) (void *, int, size_t)                 = memset;
  int   (*p_memcmp) (const void *, const void *, size_t)  = memcmp;
  void *(*p_memchr) (const void *, int, size_t)           = memchr;
  size_t(*p_strlen) (const char *)                        = strlen;
  size_t(*p_strnlen)(const char *, size_t)                = strnlen;
  int   (*p_strcmp) (const char *, const char *)          = strcmp;
  int   (*p_strncmp)(const char *, const char *, size_t)  = strncmp;
  char *(*p_strcpy) (char *, const char *)                = strcpy;
  char *(*p_strncpy)(char *, const char *, size_t)        = strncpy;
  char *(*p_strcat) (char *, const char *)                = strcat;
  char *(*p_strncat)(char *, const char *, size_t)        = strncat;
  char *(*p_strchr) (const char *, int)                   = strchr;
  char *(*p_strrchr)(const char *, int)                   = strrchr;
  char *(*p_strstr) (const char *, const char *)          = strstr;
  size_t(*p_strspn) (const char *, const char *)          = strspn;
  size_t(*p_strcspn)(const char *, const char *)          = strcspn;
  char *(*p_strpbrk)(const char *, const char *)          = strpbrk;
  char *(*p_strtok) (char *, const char *)                = strtok;
  char *(*p_strtok_r)(char *, const char *, char **)      = strtok_r;

  /* Touch every pointer so -Wunused-variable does not strip them and
   * so the symbol-set pin is observable at runtime. */
  void *sink[21];
  sink[0]  = (void *)p_memcpy;
  sink[1]  = (void *)p_memmove;
  sink[2]  = (void *)p_memset;
  sink[3]  = (void *)p_memcmp;
  sink[4]  = (void *)p_memchr;
  sink[5]  = (void *)p_strlen;
  sink[6]  = (void *)p_strnlen;
  sink[7]  = (void *)p_strcmp;
  sink[8]  = (void *)p_strncmp;
  sink[9]  = (void *)p_strcpy;
  sink[10] = (void *)p_strncpy;
  sink[11] = (void *)p_strcat;
  sink[12] = (void *)p_strncat;
  sink[13] = (void *)p_strchr;
  sink[14] = (void *)p_strrchr;
  sink[15] = (void *)p_strstr;
  sink[16] = (void *)p_strspn;
  sink[17] = (void *)p_strcspn;
  sink[18] = (void *)p_strpbrk;
  sink[19] = (void *)p_strtok;
  sink[20] = (void *)p_strtok_r;

  for (int i = 0; i < 21; i++) {
    CHECK(sink[i] != NULL, "symbol_set_pinned");
  }
  PASS("symbol_set_pinned");
}

int main(void) {
  test_memcpy_basic();
  test_memmove_overlap_forward();
  test_memmove_overlap_backward();
  test_memset_fill();
  test_memcmp_order();
  test_memchr_hit_and_miss();
  test_strlen_and_strnlen();
  test_strcmp_order();
  test_strncmp_bounded();
  test_strcpy_and_strncpy_pad();
  test_strcat_and_strncat();
  test_strchr_and_strrchr();
  test_strstr_hit_and_miss();
  test_strspn_basic();
  test_strcspn_basic();
  test_strpbrk_hit_and_miss();
  test_strtok_walks_tokens();
  test_strtok_r_reentrant_independence();
  test_symbol_set_pinned();

  if (g_failures == 0) {
    printf("TEST:PASS:clib_string\n");
    return 0;
  }
  printf("TEST:FAIL:clib_string (failures=%d)\n", g_failures);
  return 1;
}
