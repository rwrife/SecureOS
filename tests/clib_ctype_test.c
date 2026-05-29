/**
 * @file clib_ctype_test.c
 * @brief Host unit test for the freestanding ctype family
 *        (issue #407 slice 2, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Covers:
 *   1. Full 0..255 truth-table parity with a model implementation for every
 *      classification predicate (isascii, isdigit, isxdigit, isalpha,
 *      isalnum, isspace, isblank, isupper, islower, iscntrl, isprint,
 *      isgraph, ispunct).
 *   2. toupper / tolower over 0..255 — only ASCII letters flip; everything
 *      else returns unchanged.
 *   3. EOF (`-1`) returns 0 from every predicate and is passed through
 *      unchanged by the converters.
 *   4. `symbol_set_pinned`: every shipped symbol remains reachable through
 *      a function pointer, so a TinyCC drop or unrelated PR cannot silently
 *      drop a family member.
 *
 * Compiled with `-fno-builtin` so the assertions exercise OUR
 * implementations rather than `__builtin_isdigit` etc.
 *
 * Launched by:
 *   build/scripts/test_clib_ctype.sh (dispatched via
 *   build/scripts/test.sh clib_ctype).
 */

#include <stdio.h>

#include "../user/libs/clib/include/clib/ctype.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:clib_ctype:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* ---- model predicates (independent reimplementation for parity) ---- */

static int m_isdigit(int c)  { return c >= '0' && c <= '9'; }
static int m_isxdigit(int c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static int m_isupper(int c)  { return c >= 'A' && c <= 'Z'; }
static int m_islower(int c)  { return c >= 'a' && c <= 'z'; }
static int m_isalpha(int c)  { return m_isupper(c) || m_islower(c); }
static int m_isalnum(int c)  { return m_isalpha(c) || m_isdigit(c); }
static int m_isblank(int c)  { return c == ' ' || c == '\t'; }
static int m_isspace(int c)  { return c == ' ' || (c >= 9 && c <= 13); }
static int m_iscntrl(int c)  { return (c >= 0 && c <= 0x1F) || c == 0x7F; }
static int m_isprint(int c)  { return c >= 0x20 && c <= 0x7E; }
static int m_isgraph(int c)  { return c > 0x20 && c <= 0x7E; }
static int m_ispunct(int c)  { return m_isprint(c) && c != ' ' && !m_isalnum(c); }
static int m_isascii(int c)  { return (unsigned)c < 128u; }

static int m_toupper(int c)  { return m_islower(c) ? c - ('a' - 'A') : c; }
static int m_tolower(int c)  { return m_isupper(c) ? c + ('a' - 'A') : c; }

#define PARITY_PREDICATE(impl, model, name) do { \
  int ok = 1; \
  for (int c = 0; c < 256; ++c) { \
    if ((impl(c) != 0) != (model(c) != 0)) { ok = 0; break; } \
  } \
  CHECK(ok, name); \
  if (ok) printf("TEST:PASS:clib_ctype:%s\n", name); \
} while (0)

#define PARITY_CONVERT(impl, model, name) do { \
  int ok = 1; \
  for (int c = 0; c < 256; ++c) { \
    if (impl(c) != model(c)) { ok = 0; break; } \
  } \
  CHECK(ok, name); \
  if (ok) printf("TEST:PASS:clib_ctype:%s\n", name); \
} while (0)

static void test_predicates(void) {
  PARITY_PREDICATE(isascii,  m_isascii,  "isascii_full_range");
  PARITY_PREDICATE(isdigit,  m_isdigit,  "isdigit_full_range");
  PARITY_PREDICATE(isxdigit, m_isxdigit, "isxdigit_full_range");
  PARITY_PREDICATE(isalpha,  m_isalpha,  "isalpha_full_range");
  PARITY_PREDICATE(isalnum,  m_isalnum,  "isalnum_full_range");
  PARITY_PREDICATE(isspace,  m_isspace,  "isspace_full_range");
  PARITY_PREDICATE(isblank,  m_isblank,  "isblank_full_range");
  PARITY_PREDICATE(isupper,  m_isupper,  "isupper_full_range");
  PARITY_PREDICATE(islower,  m_islower,  "islower_full_range");
  PARITY_PREDICATE(iscntrl,  m_iscntrl,  "iscntrl_full_range");
  PARITY_PREDICATE(isprint,  m_isprint,  "isprint_full_range");
  PARITY_PREDICATE(isgraph,  m_isgraph,  "isgraph_full_range");
  PARITY_PREDICATE(ispunct,  m_ispunct,  "ispunct_full_range");
}

static void test_converters(void) {
  PARITY_CONVERT(toupper, m_toupper, "toupper_full_range");
  PARITY_CONVERT(tolower, m_tolower, "tolower_full_range");
}

static void test_eof(void) {
  int ok = 1;
  /* -1 = EOF: all predicates must return 0, converters must pass through. */
  if (isascii(-1))  ok = 0;
  if (isdigit(-1))  ok = 0;
  if (isxdigit(-1)) ok = 0;
  if (isalpha(-1))  ok = 0;
  if (isalnum(-1))  ok = 0;
  if (isspace(-1))  ok = 0;
  if (isblank(-1))  ok = 0;
  if (isupper(-1))  ok = 0;
  if (islower(-1))  ok = 0;
  if (iscntrl(-1))  ok = 0;
  if (isprint(-1))  ok = 0;
  if (isgraph(-1))  ok = 0;
  if (ispunct(-1))  ok = 0;
  if (toupper(-1) != -1) ok = 0;
  if (tolower(-1) != -1) ok = 0;
  CHECK(ok, "eof_safe");
  if (ok) printf("TEST:PASS:clib_ctype:eof_safe\n");
}

/* Symbol-set pinning: every shipped symbol must remain reachable through a
 * function pointer. A TinyCC drop or unrelated PR that silently removes a
 * family member will fail to link this test. The exact count (15) is the
 * pinned shape; updates must be deliberate.
 */
static void test_symbol_set_pinned(void) {
  typedef int (*pred_fn)(int);
  pred_fn preds[] = {
    isascii, isdigit, isxdigit, isalpha, isalnum,
    isspace, isblank, isupper, islower,
    iscntrl, isprint, isgraph, ispunct,
    toupper, tolower,
  };
  enum { kPinnedCount = 15 };
  int n = (int)(sizeof(preds) / sizeof(preds[0]));
  int ok = (n == kPinnedCount);
  /* Exercise every pointer so the linker cannot DCE them. */
  for (int i = 0; i < n; ++i) {
    if (preds[i] == 0) { ok = 0; break; }
    (void)preds[i]('a');
  }
  CHECK(ok, "symbol_set_pinned");
  if (ok) printf("TEST:PASS:clib_ctype:symbol_set_pinned\n");
}

int main(void) {
  test_predicates();
  test_converters();
  test_eof();
  test_symbol_set_pinned();

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:clib_ctype\n");
    return 1;
  }
  printf("TEST:PASS:clib_ctype\n");
  return 0;
}
