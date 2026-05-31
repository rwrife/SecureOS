/*
 * include/clib/limits.h
 * Freestanding userland <limits.h> nucleus (M7-TOOLCHAIN-004 slice 8,
 * issue #407, plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Purpose:
 *   `<limits.h>` is one of the freestanding headers C11 §4¶6 requires
 *   even on a non-hosted implementation. TinyCC (#408) and the in-flight
 *   freestanding `stdlib` slice (PR #428) both rely on its macros —
 *   `INT_MAX`/`INT_MIN` for the bitfield-width assertions in `tccgen.c`,
 *   `LONG_MAX`/`LONG_MIN`/`ULONG_MAX` for the `strtol`/`strtoul` overflow
 *   clamp paths, and `CHAR_BIT` for the generic shift code in `libtcc.c`.
 *
 *   Sibling slices in flight (str/mem PR #416 merged, ctype PR #417
 *   merged, qsort PR #418, stdlib PR #428, errno PR #430, stdarg PR
 *   #431, bsearch PR #433) — different header, different source file,
 *   different `symbol_set_pinned` scope. **Zero file overlap.**
 *
 * Containment:
 *   Freestanding. No libc, no kernel includes, no syscalls. Pure
 *   compile-time integer constants. The values pinned below match the
 *   x86_64 SysV ABI that TinyCC targets (`TCC_TARGET_X86_64`); the
 *   host unit test additionally asserts they match the host's
 *   `<limits.h>` so a freestanding compile cannot silently disagree
 *   with the host-side toolchain that builds it today.
 *
 * Value choices (x86_64 SysV, OS_ABI_VERSION = 0):
 *   - `CHAR_BIT = 8` — bytes are 8 bits everywhere SecureOS runs.
 *   - `char` is signed (matches gcc/clang/TinyCC default on x86_64).
 *   - `short` 16-bit, `int` 32-bit, `long` 64-bit, `long long` 64-bit.
 *
 * Why ship the header now:
 *   PR #428's `strtol`/`strtoul` clamp paths currently hard-code the
 *   limits inline because `<limits.h>` was missing; with this slice in
 *   place, that follow-up can switch to the canonical macros without
 *   moving its public symbol surface.
 *
 * Out of scope for this slice:
 *   - `<float.h>` — TinyCC's freestanding port disables FP codegen on
 *     SecureOS (`TCC_TARGET_X86_64` + no SSE state save in the launcher
 *     trap path), so floating-point limit macros are not yet required.
 *   - Wide-character limits (`MB_LEN_MAX` etc.) — no wchar in the v0
 *     userland surface.
 */

#ifndef CLIB_LIMITS_H
#define CLIB_LIMITS_H

/* ---- byte width -------------------------------------------------------- */

#define CHAR_BIT   8

/* ---- char (signed on x86_64) ------------------------------------------ */

#define SCHAR_MIN  (-128)
#define SCHAR_MAX  127
#define UCHAR_MAX  255

#define CHAR_MIN   SCHAR_MIN
#define CHAR_MAX   SCHAR_MAX

/* ---- short (16-bit) ---------------------------------------------------- */

#define SHRT_MIN   (-32768)
#define SHRT_MAX   32767
#define USHRT_MAX  65535

/* ---- int (32-bit) ------------------------------------------------------ */

/* Spelled as (-INT_MAX - 1) to avoid the well-known signed-overflow
 * UB when the literal `-2147483648` is parsed as `-(2147483648)`. */
#define INT_MIN    (-INT_MAX - 1)
#define INT_MAX    2147483647
#define UINT_MAX   4294967295U

/* ---- long (64-bit on x86_64 SysV) -------------------------------------- */

#define LONG_MIN   (-LONG_MAX - 1L)
#define LONG_MAX   9223372036854775807L
#define ULONG_MAX  18446744073709551615UL

/* ---- long long (64-bit) ------------------------------------------------ */

#define LLONG_MIN  (-LLONG_MAX - 1LL)
#define LLONG_MAX  9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL

/* ---- helper TUs (drift anchors) ---------------------------------------
 *
 * Limit macros are not symbols, so `symbol_set_pinned` cannot anchor
 * them directly. Two `clib_limits_*` helper functions defined in
 * src/limits.c give the host unit test concrete linker symbols whose
 * return values fold each shipped macro. Same shape PR #431 used for
 * `<stdarg.h>`'s `clib_stdarg_sum_*` helpers.
 */

int  clib_limits_char_bit(void);
long clib_limits_check_value(int which);

/* `which` enum for clib_limits_check_value. Kept in the header so the
 * test cannot drift from the source independently of the macro set. */
enum {
  CLIB_LIMITS_SCHAR_MIN = 0,
  CLIB_LIMITS_SCHAR_MAX = 1,
  CLIB_LIMITS_UCHAR_MAX = 2,
  CLIB_LIMITS_SHRT_MIN  = 3,
  CLIB_LIMITS_SHRT_MAX  = 4,
  CLIB_LIMITS_USHRT_MAX = 5,
  CLIB_LIMITS_INT_MIN   = 6,
  CLIB_LIMITS_INT_MAX   = 7,
  /* UINT_MAX returned via clib_limits_check_value modulo cast to long;
   * the test reads the raw macro for the bit-exact check. */
  CLIB_LIMITS_LONG_MIN  = 8,
  CLIB_LIMITS_LONG_MAX  = 9,
  CLIB_LIMITS_COUNT     = 10
};

#endif /* CLIB_LIMITS_H */
