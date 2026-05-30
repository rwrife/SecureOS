/**
 * @file clib_stddef_test.c
 * @brief Host unit test for the freestanding <stddef.h> nucleus
 *        (issue #407 slice 9, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Covers:
 *   1. NULL is defined and compares equal to a null pointer constant.
 *   2. size_t / ptrdiff_t / wchar_t are bound to the compiler builtins
 *      and have the expected x86_64 SysV widths (sizeof == 8 / 8 / 4).
 *   3. max_align_t exists, is at least 16-byte aligned (matches
 *      long double on x86_64 SysV), and its sizeof is a multiple of
 *      its alignment.
 *   4. offsetof() resolves a member's byte offset correctly through
 *      the helper TU `clib_stddef_offsetof_probe` whose probe struct
 *      lives in src/stddef.c (so the test pins the OFFSET that
 *      LINKED libclib observes, not just what this TU includes).
 *   5. Helper TUs (`clib_stddef_sizeof`, `clib_stddef_offsetof_probe`)
 *      are reachable through function pointers so a future drift
 *      cannot silently drop them (`symbol_set_pinned`).
 *
 * Compiled with `-fno-builtin` so the assertions exercise OUR header
 * typedefs / macros rather than `__builtin_offsetof` shortcuts unless
 * the header itself opted into one.
 *
 * Launched by:
 *   build/scripts/test_clib_stddef.sh (dispatched via
 *   build/scripts/test.sh clib_stddef).
 */

#include <stdio.h>

/* OUR freestanding header. Pulled in standalone (no host `<stddef.h>`)
 * so the textual constants and typedefs in this TU are exactly what
 * `libclib` ships. The separately-compiled src/stddef.c TU is also
 * linked in: the `clib_stddef_*` helpers fold sizeof/offsetof at
 * THAT TU's compile time, so the test verifies the layout as the
 * LINKED `libclib` sees it, not just what this TU happened to
 * include. */
#include "../user/libs/clib/include/clib/stddef.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:clib_stddef:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* ---- 1. NULL ----------------------------------------------------------- */
static void check_null(void) {
  void *p = NULL;
  CHECK(p == 0, "null_compares_equal_to_zero");
  CHECK((NULL) == 0, "null_literal_zero");
  /* `(void *)NULL` must compile in C — pins that NULL is a pointer
   * constant, not a 0 with no cast. */
  void *q = (void *)NULL;
  CHECK(q == p, "null_void_cast_round_trip");
}

/* ---- 2. typedef widths (x86_64 SysV) ---------------------------------- */
static void check_typedef_widths(void) {
  /* Local-TU sizeof: confirms what this TU's include sees. */
  CHECK(sizeof(size_t)    == 8, "size_t_is_8_local");
  CHECK(sizeof(ptrdiff_t) == 8, "ptrdiff_t_is_8_local");
  CHECK(sizeof(wchar_t)   == 4, "wchar_t_is_4_local");

  /* Helper-TU round-trip: confirms what the LINKED libclib sees. */
  CHECK(clib_stddef_sizeof(CLIB_STDDEF_SIZE_SIZE_T)    == 8,
        "size_t_is_8_helper");
  CHECK(clib_stddef_sizeof(CLIB_STDDEF_SIZE_PTRDIFF_T) == 8,
        "ptrdiff_t_is_8_helper");
  CHECK(clib_stddef_sizeof(CLIB_STDDEF_SIZE_WCHAR_T)   == 4,
        "wchar_t_is_4_helper");

  /* size_t / ptrdiff_t round-trip through a pointer (canonical use). */
  int   arr[3] = {1, 2, 3};
  int  *a      = &arr[0];
  int  *b      = &arr[2];
  ptrdiff_t d  = b - a;
  CHECK(d == 2, "ptrdiff_t_pointer_subtract");
  size_t s     = sizeof(arr);
  CHECK(s == 3 * sizeof(int), "size_t_sizeof_array");
}

/* ---- 3. max_align_t ---------------------------------------------------- */
static void check_max_align(void) {
  /* x86_64 SysV pins long-double alignment at 16. The header's
   * `max_align_t` union must therefore have _Alignof == 16. */
  CHECK(_Alignof(max_align_t) >= 8, "max_align_t_align_at_least_8");
  CHECK(_Alignof(max_align_t) == 16, "max_align_t_align_eq_16_x86_64");
  /* sizeof must be a positive multiple of alignment. */
  CHECK(sizeof(max_align_t) > 0, "max_align_t_size_positive");
  CHECK((sizeof(max_align_t) % _Alignof(max_align_t)) == 0,
        "max_align_t_size_multiple_of_align");

  /* Helper-TU round-trip. */
  CHECK(clib_stddef_sizeof(CLIB_STDDEF_ALIGN_MAX_ALIGN) == 16,
        "max_align_t_align_helper");
  CHECK(clib_stddef_sizeof(CLIB_STDDEF_SIZE_MAX_ALIGN) ==
            sizeof(max_align_t),
        "max_align_t_size_helper");
}

/* ---- 4. offsetof ------------------------------------------------------- */
static void check_offsetof(void) {
  /* Local probe struct — confirms the macro works on this TU. */
  struct local_probe {
    char  x;
    long  y;
    char  z;
  };
  CHECK(offsetof(struct local_probe, x) == 0, "offsetof_first_zero_local");
  CHECK(offsetof(struct local_probe, y) >= 1, "offsetof_second_aligned_local");
  CHECK(offsetof(struct local_probe, z) > offsetof(struct local_probe, y),
        "offsetof_third_after_second_local");

  /* Helper-TU probe — confirms the macro works in LINKED libclib's TU
   * with its OWN probe struct (defined in src/stddef.c). */
  CHECK(clib_stddef_offsetof_probe(CLIB_STDDEF_OFF_FIRST) == 0,
        "offsetof_first_zero_helper");
  CHECK(clib_stddef_offsetof_probe(CLIB_STDDEF_OFF_SECOND) >=
            (unsigned long)_Alignof(long),
        "offsetof_second_aligned_helper");
  CHECK(clib_stddef_offsetof_probe(CLIB_STDDEF_OFF_THIRD) >
            clib_stddef_offsetof_probe(CLIB_STDDEF_OFF_SECOND),
        "offsetof_third_after_second_helper");
}

/* ---- 5. symbol_set_pinned --------------------------------------------- */
static void check_symbol_set_pinned(void) {
  unsigned long (*p_size)(int) = clib_stddef_sizeof;
  unsigned long (*p_off)(int)  = clib_stddef_offsetof_probe;

  CHECK(p_size != NULL, "symbol_sizeof_present");
  CHECK(p_off  != NULL, "symbol_offsetof_probe_present");
  CHECK(p_size(CLIB_STDDEF_SIZE_SIZE_T) == sizeof(size_t),
        "symbol_sizeof_callable");
  CHECK(p_off(CLIB_STDDEF_OFF_FIRST) == 0,
        "symbol_offsetof_callable");

  /* Unknown `which` returns 0 from sizeof helper. */
  CHECK(p_size(CLIB_STDDEF_SIZE_COUNT + 1) == 0,
        "symbol_sizeof_unknown_returns_zero");
  /* Unknown `which` returns -1 (cast) from offsetof helper. */
  CHECK(p_off(CLIB_STDDEF_OFF_COUNT + 1) == (unsigned long)-1,
        "symbol_offsetof_unknown_returns_sentinel");
}

int main(void) {
  fprintf(stdout, "TEST:START:clib_stddef\n");

  check_null();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stddef:null_defined\n");

  check_typedef_widths();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stddef:typedef_widths_pinned\n");

  check_max_align();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stddef:max_align_t_pinned\n");

  check_offsetof();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stddef:offsetof_works\n");

  check_symbol_set_pinned();
  if (!g_fail) fprintf(stdout, "TEST:PASS:clib_stddef:symbol_set_pinned\n");

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:clib_stddef\n");
    return 1;
  }
  fprintf(stdout, "TEST:PASS:clib_stddef\n");
  return 0;
}
