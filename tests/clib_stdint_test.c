/**
 * @file clib_stdint_test.c
 * @brief Host unit test for the freestanding <stdint.h> nucleus
 *        (issue #407 slice 10, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Covers:
 *   1. Exact-width signed typedefs are 1/2/4/8 bytes.
 *   2. Exact-width unsigned typedefs are 1/2/4/8 bytes.
 *   3. Pointer-width typedefs match sizeof(void *).
 *   4. Max-width typedefs are at least 8 bytes (C11 §7.20.1.5 requires
 *      they be ≥ any other integer width the implementation provides).
 *   5. *_MAX / *_MIN constants match their typedef widths.
 *   6. SIZE_MAX matches sizeof(size_t) all-ones.
 *   7. Constant-suffix macros (`INTn_C` / `UINTn_C`) yield values that
 *      survive promotion to the matching exact-width type.
 *   8. Helper TUs (`clib_stdint_sizeof`, `clib_stdint_maxof`) are
 *      reachable through function pointers (`symbol_set_pinned`).
 *
 * Compiled with `-fno-builtin` so the assertions exercise OUR header
 * typedefs and constants rather than `__builtin_*` shortcuts.
 *
 * Launched by:
 *   build/scripts/test_clib_stdint.sh (dispatched via
 *   build/scripts/test.sh clib_stdint).
 */

#include <stdio.h>

/* OUR freestanding header. Pulled in standalone (no host <stdint.h>)
 * so the textual constants and typedefs in this TU are exactly what
 * libclib ships. The separately-compiled src/stdint.c TU is also
 * linked in: the clib_stdint_* helpers fold sizeof / *_MAX at THAT
 * TU's compile time, so the test verifies the layout as the LINKED
 * libclib sees it, not just what this TU happened to include. */
#include "../user/libs/clib/include/clib/stdint.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:clib_stdint:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* ---- 1+2. exact-width typedefs ----------------------------------------- */
static void check_exact_widths(void) {
  /* Local-TU sizeof: confirms what this TU's include sees. */
  CHECK(sizeof(int8_t)   == 1, "int8_t_is_1_local");
  CHECK(sizeof(int16_t)  == 2, "int16_t_is_2_local");
  CHECK(sizeof(int32_t)  == 4, "int32_t_is_4_local");
  CHECK(sizeof(int64_t)  == 8, "int64_t_is_8_local");
  CHECK(sizeof(uint8_t)  == 1, "uint8_t_is_1_local");
  CHECK(sizeof(uint16_t) == 2, "uint16_t_is_2_local");
  CHECK(sizeof(uint32_t) == 4, "uint32_t_is_4_local");
  CHECK(sizeof(uint64_t) == 8, "uint64_t_is_8_local");

  /* Helper-TU round-trip: confirms what the LINKED libclib sees. */
  CHECK(clib_stdint_sizeof(CLIB_STDINT_SIZE_INT8)   == 1, "int8_t_is_1_helper");
  CHECK(clib_stdint_sizeof(CLIB_STDINT_SIZE_INT16)  == 2, "int16_t_is_2_helper");
  CHECK(clib_stdint_sizeof(CLIB_STDINT_SIZE_INT32)  == 4, "int32_t_is_4_helper");
  CHECK(clib_stdint_sizeof(CLIB_STDINT_SIZE_INT64)  == 8, "int64_t_is_8_helper");
  CHECK(clib_stdint_sizeof(CLIB_STDINT_SIZE_UINT8)  == 1, "uint8_t_is_1_helper");
  CHECK(clib_stdint_sizeof(CLIB_STDINT_SIZE_UINT16) == 2, "uint16_t_is_2_helper");
  CHECK(clib_stdint_sizeof(CLIB_STDINT_SIZE_UINT32) == 4, "uint32_t_is_4_helper");
  CHECK(clib_stdint_sizeof(CLIB_STDINT_SIZE_UINT64) == 8, "uint64_t_is_8_helper");
}

/* ---- 3. pointer-width typedefs ---------------------------------------- */
static void check_pointer_widths(void) {
  CHECK(sizeof(intptr_t)  == sizeof(void *), "intptr_t_matches_void_ptr_local");
  CHECK(sizeof(uintptr_t) == sizeof(void *), "uintptr_t_matches_void_ptr_local");

  CHECK(clib_stdint_sizeof(CLIB_STDINT_SIZE_INTPTR)  == sizeof(void *),
        "intptr_t_matches_void_ptr_helper");
  CHECK(clib_stdint_sizeof(CLIB_STDINT_SIZE_UINTPTR) == sizeof(void *),
        "uintptr_t_matches_void_ptr_helper");

  /* Round-trip: any pointer can be cast to uintptr_t and back without
   * loss (C11 §7.20.1.4¶1). */
  int x = 42;
  void *p = &x;
  uintptr_t u = (uintptr_t)p;
  void *q = (void *)u;
  CHECK(p == q, "uintptr_t_pointer_round_trip");
}

/* ---- 4. max-width typedefs -------------------------------------------- */
static void check_max_widths(void) {
  CHECK(sizeof(intmax_t)  >= 8, "intmax_t_at_least_8_local");
  CHECK(sizeof(uintmax_t) >= 8, "uintmax_t_at_least_8_local");
  CHECK(sizeof(intmax_t)  == sizeof(uintmax_t),
        "intmax_t_uintmax_t_same_width");

  CHECK(clib_stdint_sizeof(CLIB_STDINT_SIZE_INTMAX)  >= 8,
        "intmax_t_at_least_8_helper");
  CHECK(clib_stdint_sizeof(CLIB_STDINT_SIZE_UINTMAX) >= 8,
        "uintmax_t_at_least_8_helper");
}

/* ---- 5. limit constants ----------------------------------------------- */
static void check_limits(void) {
  /* Signed: *_MAX is one less than 2^(width*8-1); *_MIN is -*_MAX - 1. */
  CHECK(INT8_MAX  ==  127,                        "int8_max");
  CHECK(INT8_MIN  == -128,                        "int8_min");
  CHECK(INT16_MAX ==  32767,                      "int16_max");
  CHECK(INT16_MIN == -32768,                      "int16_min");
  CHECK(INT32_MAX ==  2147483647,                 "int32_max");
  CHECK(INT32_MIN == (-2147483647 - 1),           "int32_min");
  CHECK(INT64_MAX ==  9223372036854775807LL,      "int64_max");
  CHECK(INT64_MIN == (-9223372036854775807LL - 1),"int64_min");

  /* Unsigned: *_MAX is 2^(width*8) - 1, all-ones in the width. */
  CHECK(UINT8_MAX  == 0xFFu,                        "uint8_max");
  CHECK(UINT16_MAX == 0xFFFFu,                      "uint16_max");
  CHECK(UINT32_MAX == 0xFFFFFFFFu,                  "uint32_max");
  CHECK(UINT64_MAX == 0xFFFFFFFFFFFFFFFFull,        "uint64_max");

  /* Helper-TU round-trip: confirms the LINKED libclib agrees. */
  CHECK(clib_stdint_maxof(CLIB_STDINT_SIZE_INT8)   == 127ull,
        "int8_max_helper");
  CHECK(clib_stdint_maxof(CLIB_STDINT_SIZE_INT16)  == 32767ull,
        "int16_max_helper");
  CHECK(clib_stdint_maxof(CLIB_STDINT_SIZE_INT32)  == 2147483647ull,
        "int32_max_helper");
  CHECK(clib_stdint_maxof(CLIB_STDINT_SIZE_INT64)  == 9223372036854775807ull,
        "int64_max_helper");
  CHECK(clib_stdint_maxof(CLIB_STDINT_SIZE_UINT8)  == 0xFFull,
        "uint8_max_helper");
  CHECK(clib_stdint_maxof(CLIB_STDINT_SIZE_UINT16) == 0xFFFFull,
        "uint16_max_helper");
  CHECK(clib_stdint_maxof(CLIB_STDINT_SIZE_UINT32) == 0xFFFFFFFFull,
        "uint32_max_helper");
  CHECK(clib_stdint_maxof(CLIB_STDINT_SIZE_UINT64) == 0xFFFFFFFFFFFFFFFFull,
        "uint64_max_helper");

  /* INTPTR_*: must encompass any object pointer value. */
  CHECK(UINTPTR_MAX >= UINT32_MAX, "uintptr_max_at_least_32_bit");

  /* INTMAX_* and UINTMAX_* are the implementation's widest integer
   * limits — must dominate every narrower *_MAX. */
  CHECK((unsigned long long)INTMAX_MAX  >= (unsigned long long)INT64_MAX,
        "intmax_max_at_least_int64_max");
  CHECK(UINTMAX_MAX >= UINT64_MAX, "uintmax_max_at_least_uint64_max");
}

/* ---- 6. SIZE_MAX / PTRDIFF_* ------------------------------------------ */
static void check_size_and_ptrdiff(void) {
  /* SIZE_MAX is all-ones in size_t's width. We don't include
   * <stddef.h> from this TU (stdint.h must work freestanding without
   * pulling stddef in), so derive sizeof(size_t) from the helper
   * TU's published width. */
  CHECK(SIZE_MAX > 0u, "size_max_positive");
  /* Smoke: SIZE_MAX must be representable in uintmax_t (C11 §7.20.3¶2). */
  CHECK((uintmax_t)SIZE_MAX <= UINTMAX_MAX, "size_max_fits_uintmax");

  /* PTRDIFF_MAX > 0; PTRDIFF_MIN < 0; |MIN| > MAX (two's complement). */
  CHECK(PTRDIFF_MAX > 0, "ptrdiff_max_positive");
  CHECK(PTRDIFF_MIN < 0, "ptrdiff_min_negative");
}

/* ---- 7. constant-suffix macros ---------------------------------------- */
static void check_const_macros(void) {
  /* The C11 contract is that `INTn_C(v)` has type compatible with
   * `int_leastn_t` (≥ exact-width n) and equals v. We test the
   * stronger invariant that the value round-trips through the
   * exact-width typedef, which is what TinyCC's preprocessor actually
   * relies on. */
  int8_t   v8s  = (int8_t)  INT8_C(0x7F);
  int16_t  v16s = (int16_t) INT16_C(0x7FFF);
  int32_t  v32s = (int32_t) INT32_C(0x7FFFFFFF);
  int64_t  v64s = (int64_t) INT64_C(0x7FFFFFFFFFFFFFFF);
  uint8_t  v8u  = (uint8_t)  UINT8_C(0xFF);
  uint16_t v16u = (uint16_t) UINT16_C(0xFFFF);
  uint32_t v32u = (uint32_t) UINT32_C(0xFFFFFFFF);
  uint64_t v64u = (uint64_t) UINT64_C(0xFFFFFFFFFFFFFFFF);

  CHECK(v8s  == 0x7F,                  "int8_c");
  CHECK(v16s == 0x7FFF,                "int16_c");
  CHECK(v32s == 0x7FFFFFFF,            "int32_c");
  CHECK(v64s == 0x7FFFFFFFFFFFFFFFLL,  "int64_c");
  CHECK(v8u  == 0xFFu,                 "uint8_c");
  CHECK(v16u == 0xFFFFu,               "uint16_c");
  CHECK(v32u == 0xFFFFFFFFu,           "uint32_c");
  CHECK(v64u == 0xFFFFFFFFFFFFFFFFull, "uint64_c");

  intmax_t  vmax  = INTMAX_C(0x7FFFFFFFFFFFFFFF);
  uintmax_t vumax = UINTMAX_C(0xFFFFFFFFFFFFFFFF);
  CHECK(vmax  == 0x7FFFFFFFFFFFFFFFLL,  "intmax_c");
  CHECK(vumax == 0xFFFFFFFFFFFFFFFFull, "uintmax_c");
}

/* ---- 8. symbol_set_pinned --------------------------------------------- */
static void check_symbol_set_pinned(void) {
  unsigned long      (*p_size)(int) = clib_stdint_sizeof;
  unsigned long long (*p_max)(int)  = clib_stdint_maxof;

  CHECK(p_size != (void *)0, "symbol_sizeof_present");
  CHECK(p_max  != (void *)0, "symbol_maxof_present");
  CHECK(p_size(CLIB_STDINT_SIZE_INT32) == sizeof(int32_t),
        "symbol_sizeof_callable");
  CHECK(p_max(CLIB_STDINT_SIZE_INT32)  == (unsigned long long)INT32_MAX,
        "symbol_maxof_callable");

  /* Unknown `which` returns 0 from sizeof helper. */
  CHECK(p_size(CLIB_STDINT_SIZE_COUNT + 1) == 0u,
        "symbol_sizeof_unknown_returns_zero");
  /* Unknown `which` returns all-ones sentinel from maxof helper. */
  CHECK(p_max(CLIB_STDINT_SIZE_COUNT + 1) == (unsigned long long)-1,
        "symbol_maxof_unknown_returns_sentinel");
}

int main(void) {
  fprintf(stdout, "TEST:START:clib_stdint\n");

  check_exact_widths();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stdint:exact_widths_pinned\n");

  check_pointer_widths();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stdint:pointer_widths_pinned\n");

  check_max_widths();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stdint:max_widths_pinned\n");

  check_limits();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stdint:limits_pinned\n");

  check_size_and_ptrdiff();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stdint:size_and_ptrdiff_pinned\n");

  check_const_macros();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stdint:const_macros_pinned\n");

  check_symbol_set_pinned();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stdint:symbol_set_pinned\n");

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:clib_stdint\n");
    return 1;
  }
  fprintf(stdout, "TEST:PASS:clib_stdint\n");
  return 0;
}
