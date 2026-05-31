/**
 * @file include/clib/stdint.h
 * @brief Freestanding <stdint.h> nucleus for user/libs/clib
 *        (M7-TOOLCHAIN-004 slice 10, issue #407).
 *
 * Purpose:
 *   Slice 10 of M7-TOOLCHAIN-004 (`user/libs/clib` freestanding libc
 *   subset, plan `plans/2026-05-28-in-os-toolchain-self-hosting.md`
 *   P3). TinyCC ([#408](https://github.com/rwrife/SecureOS/issues/408))
 *   and the in-OS toolchain consume `int{8,16,32,64}_t`,
 *   `uint{8,16,32,64}_t`, `intptr_t`, `uintptr_t`, `ptrdiff_t`-adjacent
 *   limits, and the `INT*_MAX` / `UINT*_MAX` constants pervasively
 *   (tcc parses them in its own headers and in any non-trivial C
 *   source it compiles).
 *
 *   C11 §7.20 mandates `<stdint.h>` even on a freestanding
 *   implementation (§4¶6 lists it alongside `<float.h>`, `<limits.h>`,
 *   `<stdarg.h>`, `<stdbool.h>`, `<stddef.h>`, `<iso646.h>`,
 *   `<stdalign.h>`, `<stdnoreturn.h>`) — same shape as the `<limits.h>`
 *   nucleus landed in slice 8 (PR #434) and the `<stddef.h>` nucleus
 *   filed in slice 9 (PR #436).
 *
 * Containment:
 *   - Freestanding. No libc, no kernel includes, no syscalls.
 *   - Resolves all typedefs through the compiler-provided
 *     `__INT*_TYPE__` / `__UINT*_TYPE__` / `__INTPTR_TYPE__` /
 *     `__UINTPTR_TYPE__` builtins that both Clang and GCC define on
 *     every target the project ships against (x86_64-unknown-none-elf
 *     for the cross-compiler today; the host gcc/clang for the unit
 *     test). This avoids hard-coding an int/long mapping that would
 *     differ between ILP32 and LP64.
 *   - All `*_MAX` / `*_MIN` constants are derived from the matching
 *     `__INT*_MAX__` / `__UINT*_MAX__` compiler builtins, same
 *     mechanism used by glibc's, musl's, and clang's own
 *     `<stdint.h>`. No magic numbers in this header.
 *
 * Symbol coverage (slice 10 + slice 10b):
 *   - Exact-width signed:   int8_t,  int16_t,  int32_t,  int64_t
 *   - Exact-width unsigned: uint8_t, uint16_t, uint32_t, uint64_t
 *   - Least-width signed:   int_least{8,16,32,64}_t            (slice 10b)
 *   - Least-width unsigned: uint_least{8,16,32,64}_t           (slice 10b)
 *   - Fast-width signed:    int_fast{8,16,32,64}_t             (slice 10b)
 *   - Fast-width unsigned:  uint_fast{8,16,32,64}_t            (slice 10b)
 *   - Pointer-width:        intptr_t, uintptr_t
 *   - Max-width:            intmax_t, uintmax_t
 *   - Limits:               INT{8,16,32,64}_{MIN,MAX},
 *                           UINT{8,16,32,64}_MAX,
 *                           INT_LEAST{8,16,32,64}_{MIN,MAX},     (slice 10b)
 *                           UINT_LEAST{8,16,32,64}_MAX,          (slice 10b)
 *                           INT_FAST{8,16,32,64}_{MIN,MAX},      (slice 10b)
 *                           UINT_FAST{8,16,32,64}_MAX,           (slice 10b)
 *                           INTPTR_{MIN,MAX}, UINTPTR_MAX,
 *                           INTMAX_{MIN,MAX}, UINTMAX_MAX
 *   - Constant macros:      INT{8,16,32,64}_C(v), UINT{8,16,32,64}_C(v),
 *                           INTMAX_C(v), UINTMAX_C(v)
 *   - SIZE_MAX, PTRDIFF_{MIN,MAX}
 *
 * Slice 10b note (least- / fast- width families):
 *   C11 §7.20.1.2/3 require these alongside the exact-width set, and
 *   the constant-suffix macros `INTn_C(v)` / `UINTn_C(v)` are formally
 *   typed as `int_leastn_t` / `uint_leastn_t` (§7.20.4.1), so slice 10
 *   was technically already relying on them implicitly. Slice 10b
 *   makes them present-and-pinned through the same compiler-builtin
 *   path (`__INT_LEAST*_TYPE__` / `__INT_LEAST*_MAX__` and
 *   `__INT_FAST*_TYPE__` / `__INT_FAST*_MAX__`) every modern Clang /
 *   GCC / TinyCC defines for the supported targets, and adds drift
 *   anchors + a `least_fast_pinned` sub-marker to the host unit test.
 *   No ABI bump (additive optional surface, mirrors the no-bump
 *   discipline shared with every other slice 10/11/12 nucleus).
 *
 * Out of scope for this slice (folded in by later #407 slices once a
 * TinyCC drop forces them):
 *   - `<inttypes.h>` (printf / scanf format-string macros). Lives on
 *     top of `<stdio.h>`, which is itself deferred to the M7-TOOLCHAIN-004
 *     stdio slice.
 *   - `wint_t` / `WINT_MIN` / `WINT_MAX`. Belongs alongside `wchar_t`
 *     in the `<wchar.h>` slice, which has no in-tree consumer today.
 *
 * Naming:
 *   Canonical C11 names so TinyCC and other consumers link / parse
 *   for free.
 *
 * ABI status:
 *   Userland-only. Does **not** bump `OS_ABI_VERSION` (parity with
 *   the `<limits.h>`, `<stddef.h>`, `<stdarg.h>`, `<stdbool.h>`,
 *   `<errno.h>`, and `stdlib` slices).
 *
 * Issue: #407. Refs umbrella #403. Plan: P3 in
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md`.
 */

#ifndef SECUREOS_USER_LIBS_CLIB_STDINT_H
#define SECUREOS_USER_LIBS_CLIB_STDINT_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- exact-width integer types ----------------------------------------- *
 *
 * Resolved through the compiler-provided `__INT*_TYPE__` builtins so the
 * header is target-correct on both the x86_64 cross-compiler and the
 * host gcc/clang the unit test runs under. Both Clang and GCC define
 * these for every supported target (Clang since 3.0, GCC since 4.5),
 * and TinyCC defines them in its own `tccpp.c` predefines table —
 * which is exactly the link surface this header is being staged for.
 */

typedef __INT8_TYPE__   int8_t;
typedef __INT16_TYPE__  int16_t;
typedef __INT32_TYPE__  int32_t;
typedef __INT64_TYPE__  int64_t;

typedef __UINT8_TYPE__  uint8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __UINT64_TYPE__ uint64_t;

/* --- pointer-width / max-width integer types --------------------------- */

typedef __INTPTR_TYPE__  intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;

typedef __INTMAX_TYPE__  intmax_t;
typedef __UINTMAX_TYPE__ uintmax_t;

/* --- least-width integer types (C11 §7.20.1.2, slice 10b) -------------- *
 *
 * `int_leastN_t` is the *smallest* integer type with at least N bits.
 * Required to exist for N in {8, 16, 32, 64} (C11 §7.20.1.2¶3). The
 * compiler exposes `__INT_LEASTN_TYPE__` / `__UINT_LEASTN_TYPE__` for
 * every supported target; using them keeps the typedef target-correct
 * on both the x86_64 cross-compiler and the host gcc/clang.
 *
 * On every target the project ships against today the least-width
 * typedef is the matching exact-width one, but encoding that
 * assumption locally would defeat the purpose of the typedef — defer
 * to the compiler builtin.
 */

typedef __INT_LEAST8_TYPE__   int_least8_t;
typedef __INT_LEAST16_TYPE__  int_least16_t;
typedef __INT_LEAST32_TYPE__  int_least32_t;
typedef __INT_LEAST64_TYPE__  int_least64_t;

typedef __UINT_LEAST8_TYPE__  uint_least8_t;
typedef __UINT_LEAST16_TYPE__ uint_least16_t;
typedef __UINT_LEAST32_TYPE__ uint_least32_t;
typedef __UINT_LEAST64_TYPE__ uint_least64_t;

/* --- fast-width integer types (C11 §7.20.1.3, slice 10b) --------------- *
 *
 * `int_fastN_t` is the implementation's *fastest* integer type with
 * at least N bits. Required for the same N ∈ {8, 16, 32, 64} set
 * (C11 §7.20.1.3¶2). On x86_64 the canonical choice for N > 8 is
 * `long`, which is what both Clang and GCC expose via
 * `__INT_FASTN_TYPE__`.
 */

typedef __INT_FAST8_TYPE__    int_fast8_t;
typedef __INT_FAST16_TYPE__   int_fast16_t;
typedef __INT_FAST32_TYPE__   int_fast32_t;
typedef __INT_FAST64_TYPE__   int_fast64_t;

typedef __UINT_FAST8_TYPE__   uint_fast8_t;
typedef __UINT_FAST16_TYPE__  uint_fast16_t;
typedef __UINT_FAST32_TYPE__  uint_fast32_t;
typedef __UINT_FAST64_TYPE__  uint_fast64_t;

/* --- exact-width limits ------------------------------------------------ *
 *
 * The compiler exposes one positive constant per width
 * (`__INT8_MAX__`, `__UINT8_MAX__`, ...). C11 §7.20.2.1 defines the
 * signed *_MIN as `-(*_MAX) - 1` (two's complement), which we encode
 * literally to keep the header self-evidently correct.
 */

#define INT8_MAX   __INT8_MAX__
#define INT16_MAX  __INT16_MAX__
#define INT32_MAX  __INT32_MAX__
#define INT64_MAX  __INT64_MAX__

#define INT8_MIN   (-INT8_MAX  - 1)
#define INT16_MIN  (-INT16_MAX - 1)
#define INT32_MIN  (-INT32_MAX - 1)
#define INT64_MIN  (-INT64_MAX - 1)

#define UINT8_MAX  __UINT8_MAX__
#define UINT16_MAX __UINT16_MAX__
#define UINT32_MAX __UINT32_MAX__
#define UINT64_MAX __UINT64_MAX__

/* --- least- / fast-width limits (C11 §7.20.2.2/3, slice 10b) ----------- *
 *
 * Each `INT_LEASTn_MAX` / `INT_FASTn_MAX` is at least 2^(n-1) - 1 and
 * each `UINT_LEASTn_MAX` / `UINT_FASTn_MAX` is at least 2^n - 1, but
 * may be larger if the chosen typedef is wider than n bits (e.g.
 * `int_fast16_t == long` on x86_64). The compiler builtin exposes
 * the *actual* width's max so the macro stays self-consistent with
 * the typedef.
 */

#define INT_LEAST8_MAX   __INT_LEAST8_MAX__
#define INT_LEAST16_MAX  __INT_LEAST16_MAX__
#define INT_LEAST32_MAX  __INT_LEAST32_MAX__
#define INT_LEAST64_MAX  __INT_LEAST64_MAX__

#define INT_LEAST8_MIN   (-INT_LEAST8_MAX  - 1)
#define INT_LEAST16_MIN  (-INT_LEAST16_MAX - 1)
#define INT_LEAST32_MIN  (-INT_LEAST32_MAX - 1)
#define INT_LEAST64_MIN  (-INT_LEAST64_MAX - 1)

#define UINT_LEAST8_MAX  __UINT_LEAST8_MAX__
#define UINT_LEAST16_MAX __UINT_LEAST16_MAX__
#define UINT_LEAST32_MAX __UINT_LEAST32_MAX__
#define UINT_LEAST64_MAX __UINT_LEAST64_MAX__

#define INT_FAST8_MAX    __INT_FAST8_MAX__
#define INT_FAST16_MAX   __INT_FAST16_MAX__
#define INT_FAST32_MAX   __INT_FAST32_MAX__
#define INT_FAST64_MAX   __INT_FAST64_MAX__

#define INT_FAST8_MIN    (-INT_FAST8_MAX  - 1)
#define INT_FAST16_MIN   (-INT_FAST16_MAX - 1)
#define INT_FAST32_MIN   (-INT_FAST32_MAX - 1)
#define INT_FAST64_MIN   (-INT_FAST64_MAX - 1)

#define UINT_FAST8_MAX   __UINT_FAST8_MAX__
#define UINT_FAST16_MAX  __UINT_FAST16_MAX__
#define UINT_FAST32_MAX  __UINT_FAST32_MAX__
#define UINT_FAST64_MAX  __UINT_FAST64_MAX__

/* --- pointer-width / max-width limits ---------------------------------- */

#define INTPTR_MAX  __INTPTR_MAX__
#define INTPTR_MIN  (-INTPTR_MAX - 1)
#define UINTPTR_MAX __UINTPTR_MAX__

#define INTMAX_MAX  __INTMAX_MAX__
#define INTMAX_MIN  (-INTMAX_MAX - 1)
#define UINTMAX_MAX __UINTMAX_MAX__

/* --- size_t / ptrdiff_t-adjacent limits -------------------------------- *
 *
 * `SIZE_MAX` and `PTRDIFF_{MIN,MAX}` are required by C11 §7.20.3 to
 * live in `<stdint.h>` even though the typedefs themselves live in
 * `<stddef.h>` (slice 9, PR #436). The compiler exposes the matching
 * builtins on both Clang and GCC; the unit test pins SIZE_MAX against
 * sizeof-arithmetic so a future drop where the builtin disappears
 * does not silently regress to a too-small constant.
 */

#define SIZE_MAX     __SIZE_MAX__
#define PTRDIFF_MAX  __PTRDIFF_MAX__
#define PTRDIFF_MIN  (-PTRDIFF_MAX - 1)

/* --- constant-suffix macros (C11 §7.20.4) ------------------------------ *
 *
 * `INTn_C(v)` / `UINTn_C(v)` glue a value with the suffix that
 * promotes it to the matching exact-width type. The compiler exposes
 * the canonical suffix via `__INTn_C_SUFFIX__` / `__UINTn_C_SUFFIX__`
 * (Clang) or via fixed mappings (GCC). We use the lowest-common-
 * denominator approach that both implementations document: paste the
 * compiler's `__INTn_C` / `__UINTn_C` builtin when present, else fall
 * through to the trivial `v` (acceptable for n ≤ INT width).
 *
 * Note: GCC and Clang both define `__INTn_C` / `__UINTn_C` builtins
 * for every n the project ships against today; the fallback exists
 * only to make this header robust against a freestanding cross
 * toolchain that opts out of the builtins (none in-tree as of
 * 2026-05-30).
 */

#ifdef __INT8_C
# define INT8_C(v)   __INT8_C(v)
#else
# define INT8_C(v)   (v)
#endif
#ifdef __INT16_C
# define INT16_C(v)  __INT16_C(v)
#else
# define INT16_C(v)  (v)
#endif
#ifdef __INT32_C
# define INT32_C(v)  __INT32_C(v)
#else
# define INT32_C(v)  (v)
#endif
#ifdef __INT64_C
# define INT64_C(v)  __INT64_C(v)
#else
# define INT64_C(v)  (v ## LL)
#endif

#ifdef __UINT8_C
# define UINT8_C(v)  __UINT8_C(v)
#else
# define UINT8_C(v)  (v ## U)
#endif
#ifdef __UINT16_C
# define UINT16_C(v) __UINT16_C(v)
#else
# define UINT16_C(v) (v ## U)
#endif
#ifdef __UINT32_C
# define UINT32_C(v) __UINT32_C(v)
#else
# define UINT32_C(v) (v ## U)
#endif
#ifdef __UINT64_C
# define UINT64_C(v) __UINT64_C(v)
#else
# define UINT64_C(v) (v ## ULL)
#endif

#ifdef __INTMAX_C
# define INTMAX_C(v)  __INTMAX_C(v)
#else
# define INTMAX_C(v)  (v ## LL)
#endif
#ifdef __UINTMAX_C
# define UINTMAX_C(v) __UINTMAX_C(v)
#else
# define UINTMAX_C(v) (v ## ULL)
#endif

/* --- helper TU (drift anchor) ------------------------------------------ *
 *
 * The companion `src/stdint.c` TU exposes a `clib_stdint_sizeof`
 * helper that folds `sizeof(<typedef>)` at THAT TU's compile time
 * (not this one's). The unit test calls through a function pointer
 * so a future drop that removes the typedef or silently changes its
 * width fails the `symbol_set_pinned` sub-marker — same shape used by
 * the `<stddef.h>` / `<limits.h>` slices.
 */

enum {
  CLIB_STDINT_SIZE_INT8 = 0,
  CLIB_STDINT_SIZE_INT16,
  CLIB_STDINT_SIZE_INT32,
  CLIB_STDINT_SIZE_INT64,
  CLIB_STDINT_SIZE_UINT8,
  CLIB_STDINT_SIZE_UINT16,
  CLIB_STDINT_SIZE_UINT32,
  CLIB_STDINT_SIZE_UINT64,
  CLIB_STDINT_SIZE_INTPTR,
  CLIB_STDINT_SIZE_UINTPTR,
  CLIB_STDINT_SIZE_INTMAX,
  CLIB_STDINT_SIZE_UINTMAX,
  /* slice 10b — least-width family */
  CLIB_STDINT_SIZE_INT_LEAST8,
  CLIB_STDINT_SIZE_INT_LEAST16,
  CLIB_STDINT_SIZE_INT_LEAST32,
  CLIB_STDINT_SIZE_INT_LEAST64,
  CLIB_STDINT_SIZE_UINT_LEAST8,
  CLIB_STDINT_SIZE_UINT_LEAST16,
  CLIB_STDINT_SIZE_UINT_LEAST32,
  CLIB_STDINT_SIZE_UINT_LEAST64,
  /* slice 10b — fast-width family */
  CLIB_STDINT_SIZE_INT_FAST8,
  CLIB_STDINT_SIZE_INT_FAST16,
  CLIB_STDINT_SIZE_INT_FAST32,
  CLIB_STDINT_SIZE_INT_FAST64,
  CLIB_STDINT_SIZE_UINT_FAST8,
  CLIB_STDINT_SIZE_UINT_FAST16,
  CLIB_STDINT_SIZE_UINT_FAST32,
  CLIB_STDINT_SIZE_UINT_FAST64,
  CLIB_STDINT_SIZE_COUNT
};

/**
 * @brief Return the byte width of the requested `<stdint.h>` typedef,
 *        as observed by the freestanding `libclib` TU that defines
 *        this helper.
 *
 * @param which One of the `CLIB_STDINT_SIZE_*` enumerators.
 * @return The matching `sizeof` value, or 0 for an out-of-range
 *         enumerator (drift sentinel).
 */
unsigned long clib_stdint_sizeof(int which);

/**
 * @brief Return the maximum positive value the requested typedef can
 *        represent, as observed by the freestanding `libclib` TU.
 *
 * Used by the unit test's `limits_pinned` sub-marker to confirm that
 * the header's `*_MAX` macros and the typedef widths agree, so a
 * future PR that bumps a width without bumping its limit (or vice
 * versa) is caught at host-test time.
 *
 * @return The matching `*_MAX` value cast to `unsigned long long`,
 *         or `(unsigned long long)-1` (all-ones sentinel) for an
 *         out-of-range enumerator. Signed *_MAX values are
 *         non-negative and therefore fit losslessly in the unsigned
 *         carrier on every target the project ships against.
 */
unsigned long long clib_stdint_maxof(int which);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_STDINT_H */
