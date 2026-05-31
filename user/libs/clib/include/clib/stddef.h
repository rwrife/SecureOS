/*
 * include/clib/stddef.h
 * Freestanding userland <stddef.h> nucleus (M7-TOOLCHAIN-004 slice 9,
 * issue #407, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Purpose:
 *   `<stddef.h>` is one of the freestanding headers C11 §4¶6 requires
 *   even on a non-hosted implementation. TinyCC (#408) consumes it
 *   pervasively (`size_t` for `tcc_malloc`/`tcc_realloc` signatures,
 *   `ptrdiff_t` for the relocation deltas in `tccelf.c`, `offsetof` for
 *   the section-header walkers, and `NULL` essentially everywhere), and
 *   every previously-merged slice of #407 reaches for `size_t` through
 *   the host `<stddef.h>` because OUR `<clib/stddef.h>` was missing.
 *   With this slice in place those consumers can switch over without
 *   moving their public symbol surface, and the on-target build can
 *   stop depending on whichever `<stddef.h>` the host compiler ships.
 *
 *   Sibling slices of #407 that have landed or are in flight (str/mem
 *   PR #416 merged, ctype PR #417 merged, qsort PR #418 merged, stdlib
 *   PR #428, errno PR #430, stdarg PR #431, bsearch PR #433, limits PR
 *   #434, stdbool PR #435) — different header, different source file,
 *   different `symbol_set_pinned` scope. **Zero file overlap.**
 *
 * Containment:
 *   Freestanding. No libc, no kernel includes, no syscalls. Pure
 *   compile-time typedefs and macros. The types are bound to the
 *   compiler builtins `__SIZE_TYPE__`, `__PTRDIFF_TYPE__`, and
 *   `__WCHAR_TYPE__` so the header is correct on both the host
 *   toolchain that builds `libclib.a` today and on TinyCC's
 *   `TCC_TARGET_X86_64` (which exposes the same builtins).
 *
 * Coverage (this slice):
 *   Macros : NULL, offsetof(type, member)
 *   Types  : size_t, ptrdiff_t, wchar_t, max_align_t
 *
 * Drift anchor:
 *   Like the `<limits.h>` slice (PR #434), the types and macros here
 *   are not linker symbols. A tiny helper TU in `src/stddef.c` exposes
 *   two `clib_stddef_*` functions whose return values fold sizeof()
 *   and offsetof() at THAT TU's compile time, so the host unit test
 *   verifies what `libclib` actually ships rather than what the test
 *   TU happened to include.
 *
 * Out of scope for this slice:
 *   - `nullptr_t` (C23) — TinyCC's port targets C11.
 *   - `unreachable` / `static_assert` macros — those live in
 *     `<assert.h>` (separate header) and `<stdnoreturn.h>` (TinyCC
 *     does not consume).
 */

#ifndef CLIB_STDDEF_H
#define CLIB_STDDEF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- NULL --------------------------------------------------------------
 *
 * Spelled as `((void *)0)` in C and as a bare 0 in C++ so the header is
 * legal in both modes (matches the gcc / clang / TinyCC convention).
 */
#ifndef NULL
#  ifdef __cplusplus
#    define NULL 0
#  else
#    define NULL ((void *)0)
#  endif
#endif

/* ---- size_t / ptrdiff_t / wchar_t -------------------------------------
 *
 * Bound to the compiler's intrinsic types so the typedef is identical
 * to whatever the host toolchain (gcc/clang) and TinyCC
 * (`TCC_TARGET_X86_64`) ship in their own `<stddef.h>`. Guarded by the
 * same `__SIZE_TYPE__` / `__PTRDIFF_TYPE__` / `__WCHAR_TYPE__` symbols
 * that the host `<stddef.h>` uses, so co-including this header and the
 * host one (which our test file deliberately avoids) does not cause a
 * `typedef redefinition` collision under `-Werror`.
 */
#ifndef _SIZE_T_DEFINED_CLIB
#define _SIZE_T_DEFINED_CLIB
typedef __SIZE_TYPE__ size_t;
#endif

#ifndef _PTRDIFF_T_DEFINED_CLIB
#define _PTRDIFF_T_DEFINED_CLIB
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#endif

#ifndef _WCHAR_T_DEFINED_CLIB
#define _WCHAR_T_DEFINED_CLIB
typedef __WCHAR_TYPE__ wchar_t;
#endif

/* ---- max_align_t -------------------------------------------------------
 *
 * C11 §7.19. On x86_64 SysV the maximally-aligned scalar is
 * `long double` (16-byte alignment). A union with both `long double`
 * and `long long` matches the layout gcc/clang/TinyCC produce so the
 * `_Alignof(max_align_t)` test below pins the same value across all
 * three compilers.
 */
typedef union {
  long long      __ll;
  long double    __ld;
  void          *__p;
  void         (*__fp)(void);
} max_align_t;

/* ---- offsetof ----------------------------------------------------------
 *
 * Uses the compiler builtin where available so TinyCC's `__builtin_*`
 * shortcut is honored; falls back to the canonical address-of-NULL
 * trick that the C standard expressly blesses for freestanding.
 */
#ifndef offsetof
#  if defined(__has_builtin)
#    if __has_builtin(__builtin_offsetof)
#      define offsetof(type, member) __builtin_offsetof(type, member)
#    endif
#  endif
#  ifndef offsetof
#    define offsetof(type, member) \
       ((size_t) & (((type *)0)->member))
#  endif
#endif

/* ---- helper TUs (drift anchors) ---------------------------------------
 *
 * Macros and typedefs are not linker symbols, so `symbol_set_pinned`
 * cannot anchor them directly. Two `clib_stddef_*` helper functions
 * defined in src/stddef.c give the host unit test concrete linker
 * symbols whose return values fold each shipped quantity. Same shape
 * PR #434 (`<limits.h>`) and PR #431 (`<stdarg.h>`) used.
 */

unsigned long clib_stddef_sizeof(int which);
unsigned long clib_stddef_offsetof_probe(int which);

/* `which` enum for clib_stddef_sizeof. Kept in the header so the test
 * cannot drift from the source independently of the type set. */
enum {
  CLIB_STDDEF_SIZE_SIZE_T     = 0,
  CLIB_STDDEF_SIZE_PTRDIFF_T  = 1,
  CLIB_STDDEF_SIZE_WCHAR_T    = 2,
  CLIB_STDDEF_SIZE_MAX_ALIGN  = 3,
  CLIB_STDDEF_ALIGN_MAX_ALIGN = 4,
  CLIB_STDDEF_SIZE_COUNT      = 5
};

/* `which` enum for clib_stddef_offsetof_probe. The probe struct lives
 * inside src/stddef.c so this enum is the only contract the test
 * cares about. */
enum {
  CLIB_STDDEF_OFF_FIRST  = 0,  /* must be 0 (first member offset) */
  CLIB_STDDEF_OFF_SECOND = 1,  /* > 0 (some packing-dependent value) */
  CLIB_STDDEF_OFF_THIRD  = 2,  /* > offsetof(second) */
  CLIB_STDDEF_OFF_COUNT  = 3
};

#ifdef __cplusplus
}
#endif

#endif /* CLIB_STDDEF_H */
