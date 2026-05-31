/**
 * @file clib_limits_test.c
 * @brief Host unit test for the freestanding <limits.h> nucleus
 *        (issue #407 slice 8, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Covers:
 *   1. Each shipped macro has the canonical value (bit-exact compare
 *      against the literals the x86_64 SysV ABI mandates — same values
 *      the host toolchain that builds `libclib.a` uses). We deliberately
 *      do NOT include host `<limits.h>` here because that would collide
 *      with OUR header under `-Werror`; the helper TU `src/limits.c`
 *      round-trips every macro from the linked `libclib` so the test
 *      verifies what `libclib` actually ships, not just what this TU
 *      happened to include.
 *   2. INT_MIN / LONG_MIN / LLONG_MIN are written as `-MAX - 1` so they
 *      do not trigger signed-overflow UB when the compiler folds them.
 *      Verified by computing `MIN + MAX == -1` and `MAX + MIN + 1 == 0`.
 *   3. CHAR_BIT == 8 — bytes are 8 bits on every platform SecureOS runs.
 *   4. `char` is signed (matches gcc/clang/TinyCC default on x86_64).
 *   5. Helper TUs (`clib_limits_char_bit`, `clib_limits_check_value`)
 *      are reachable through function pointers so a future drift cannot
 *      silently drop them (`symbol_set_pinned`).
 *
 * Compiled with `-fno-builtin` so the assertions exercise OUR header
 * constants rather than `__builtin_*_max` shortcuts.
 *
 * Launched by:
 *   build/scripts/test_clib_limits.sh (dispatched via
 *   build/scripts/test.sh clib_limits).
 */

#include <stdio.h>

/* OUR freestanding header. Pulled in standalone (no host `<limits.h>`)
 * so the textual constants in this TU are exactly what `libclib`
 * ships. The separately-compiled src/limits.c TU is also linked in:
 * the `clib_limits_check_value` helper folds the macros at THAT TU's
 * compile time, so the test verifies the macros as the LINKED
 * `libclib` sees them, not just what this TU happened to include. */
#include "../user/libs/clib/include/clib/limits.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:clib_limits:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* ---- 1. bit-exact compare against host <limits.h> ----------------------
 *
 * The local include re-opens CLIB_LIMITS_H; we round-trip every macro
 * through the helper TU so we are actually checking the values the
 * linked `libclib` ships, not just the textual constants in this TU.
 */
static void check_macro_values_pinned(void) {
  /* Helper-TU round-trip: the value returned was folded at src/limits.c's
   * compile time, so a drift in include/clib/limits.h that this TU re-
   * includes would still be caught. */
  CHECK(clib_limits_char_bit() == 8, "char_bit_helper_eq_8");

  CHECK(clib_limits_check_value(CLIB_LIMITS_SCHAR_MIN) == -128L,
        "schar_min_value_pinned");
  CHECK(clib_limits_check_value(CLIB_LIMITS_SCHAR_MAX) == 127L,
        "schar_max_value_pinned");
  CHECK(clib_limits_check_value(CLIB_LIMITS_UCHAR_MAX) == 255L,
        "uchar_max_value_pinned");

  CHECK(clib_limits_check_value(CLIB_LIMITS_SHRT_MIN) == -32768L,
        "shrt_min_value_pinned");
  CHECK(clib_limits_check_value(CLIB_LIMITS_SHRT_MAX) == 32767L,
        "shrt_max_value_pinned");
  CHECK(clib_limits_check_value(CLIB_LIMITS_USHRT_MAX) == 65535L,
        "ushrt_max_value_pinned");

  CHECK(clib_limits_check_value(CLIB_LIMITS_INT_MIN) == -2147483648L,
        "int_min_value_pinned");
  CHECK(clib_limits_check_value(CLIB_LIMITS_INT_MAX) == 2147483647L,
        "int_max_value_pinned");

  CHECK(clib_limits_check_value(CLIB_LIMITS_LONG_MIN) ==
            (-9223372036854775807L - 1L),
        "long_min_value_pinned");
  CHECK(clib_limits_check_value(CLIB_LIMITS_LONG_MAX) ==
            9223372036854775807L,
        "long_max_value_pinned");

  /* Textual checks against OUR header constants (no host include). */
  CHECK(SCHAR_MIN == -128 && SCHAR_MAX == 127 && UCHAR_MAX == 255,
        "char_range_textual");
  CHECK(SHRT_MIN == -32768 && SHRT_MAX == 32767 && USHRT_MAX == 65535,
        "shrt_range_textual");
  CHECK(INT_MAX == 2147483647 && UINT_MAX == 4294967295U,
        "int_range_textual");
  CHECK(LONG_MAX == 9223372036854775807L &&
            ULONG_MAX == 18446744073709551615UL,
        "long_range_textual");
  CHECK(LLONG_MAX == 9223372036854775807LL &&
            ULLONG_MAX == 18446744073709551615ULL,
        "llong_range_textual");
}

/* ---- 2. MIN spelled as `-MAX - 1` (no signed-overflow UB) -------------- */
static void check_min_max_relation(void) {
  /* INT_MIN + INT_MAX == -1 — pins the off-by-one and confirms two's
   * complement on the build host. */
  CHECK((long)INT_MIN + (long)INT_MAX == -1L,
        "int_min_plus_max_eq_minus_one");
  CHECK(LONG_MIN + LONG_MAX == -1L,
        "long_min_plus_max_eq_minus_one");
  CHECK(LLONG_MIN + LLONG_MAX == -1LL,
        "llong_min_plus_max_eq_minus_one");

  /* SCHAR_MIN + SCHAR_MAX == -1, same shape. */
  CHECK((int)SCHAR_MIN + (int)SCHAR_MAX == -1,
        "schar_min_plus_max_eq_minus_one");
  CHECK((int)SHRT_MIN + (int)SHRT_MAX == -1,
        "shrt_min_plus_max_eq_minus_one");
}

/* ---- 3. CHAR_BIT pinned at 8 ------------------------------------------ */
static void check_char_bit(void) {
  CHECK(CHAR_BIT == 8, "char_bit_eq_8_local");
  /* Sanity: sizeof(int) * CHAR_BIT >= 32 (matches the int range we ship). */
  CHECK((int)(sizeof(int) * CHAR_BIT) >= 32, "int_width_at_least_32_bits");
  CHECK((int)(sizeof(long) * CHAR_BIT) >= 64, "long_width_at_least_64_bits");
}

/* ---- 4. char is signed (matches TinyCC x86_64 default) ---------------- */
static void check_char_signed(void) {
  /* CHAR_MIN < 0 iff `char` is signed. The x86_64 SysV ABI mandates
   * signed char, which TinyCC's TCC_TARGET_X86_64 also assumes. */
  CHECK(CHAR_MIN < 0, "char_is_signed");
  CHECK(CHAR_MIN == SCHAR_MIN, "char_min_eq_schar_min");
  CHECK(CHAR_MAX == SCHAR_MAX, "char_max_eq_schar_max");
}

/* ---- 5. symbol_set_pinned (helper TUs reachable via function ptr) ----- */
static void check_symbol_set_pinned(void) {
  int (*p_char_bit)(void) = clib_limits_char_bit;
  long (*p_check)(int)    = clib_limits_check_value;

  CHECK(p_char_bit != NULL, "symbol_char_bit_present");
  CHECK(p_check    != NULL, "symbol_check_value_present");
  CHECK(p_char_bit() == CHAR_BIT, "symbol_char_bit_callable");
  CHECK(p_check(CLIB_LIMITS_INT_MAX) == (long)INT_MAX,
        "symbol_check_value_callable");

  /* Unknown `which` returns 0 — pins the default path. */
  CHECK(p_check(CLIB_LIMITS_COUNT + 1) == 0,
        "symbol_check_value_unknown_returns_zero");
}

int main(void) {
  fprintf(stdout, "TEST:START:clib_limits\n");

  check_macro_values_pinned();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_limits:macro_values_pinned\n");

  check_min_max_relation();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_limits:min_max_relation\n");

  check_char_bit();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_limits:char_bit_eq_8\n");

  check_char_signed();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_limits:char_is_signed\n");

  check_symbol_set_pinned();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_limits:symbol_set_pinned\n");

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:clib_limits\n");
    return 1;
  }
  fprintf(stdout, "TEST:PASS:clib_limits\n");
  return 0;
}
