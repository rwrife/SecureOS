/*
 * include/clib/float.h
 * Freestanding userland <float.h> nucleus (M7-TOOLCHAIN-004, issue #407).
 *
 * Purpose:
 *   ISO C11 §4¶6 enumerates `<float.h>` as one of the freestanding-
 *   required headers; §5.2.4.2.2 + §7.7 specify the macros it must
 *   provide. TinyCC (#408), the freestanding stdlib slice (#428), and
 *   any third-party SDK code consumed by the in-OS toolchain (#403)
 *   are entitled to `#include <float.h>` and to use the macros below
 *   in `#if` and as compile-time floating constants.
 *
 *   Peer freestanding headers landed in this libc subset use the
 *   same "header + tiny helper TU + drift-anchor host test" shape:
 *     - <stddef.h>   (PR #436)
 *     - <stdint.h>   (PR #437)
 *     - <limits.h>   (PR #434)
 *     - <stdarg.h>   (PR #431)
 *     - <stdbool.h>  (PR #435)
 *     - <iso646.h>   (PR #439)
 *     - <stdalign.h> (PR #440)
 *   This slice mirrors it for <float.h>.
 *
 * Containment:
 *   Freestanding. No libc, no kernel includes, no syscalls. Each
 *   macro forwards to a compiler-predefined constant (`__FLT_*__`,
 *   `__DBL_*__`, `__LDBL_*__`, `__FLT_RADIX__`, `__FLT_EVAL_METHOD__`,
 *   `__DEC_EVAL_METHOD__`) that GCC and clang are required to
 *   provide. Forwarding rather than hard-coding values keeps the
 *   header correct regardless of host long-double width (x86-64
 *   80-bit extended vs. AArch64 IEEE 128-bit vs. 64-bit double-as-
 *   long-double targets) and matches how glibc / musl / newlib's
 *   own `<float.h>` shims are written.
 *
 * Symbol set (pinned by the `symbol_set_pinned` host-test sub-marker;
 * matches C11 §5.2.4.2.2¶8 / §7.7 + C11 §F.2¶3 verbatim):
 *
 *   - FLT_RADIX, FLT_ROUNDS, FLT_EVAL_METHOD, FLT_HAS_SUBNORM,
 *     DBL_HAS_SUBNORM, LDBL_HAS_SUBNORM
 *   - DECIMAL_DIG
 *   - {FLT,DBL,LDBL}_MANT_DIG
 *   - {FLT,DBL,LDBL}_DECIMAL_DIG
 *   - {FLT,DBL,LDBL}_DIG
 *   - {FLT,DBL,LDBL}_MIN_EXP
 *   - {FLT,DBL,LDBL}_MIN_10_EXP
 *   - {FLT,DBL,LDBL}_MAX_EXP
 *   - {FLT,DBL,LDBL}_MAX_10_EXP
 *   - {FLT,DBL,LDBL}_MAX
 *   - {FLT,DBL,LDBL}_EPSILON
 *   - {FLT,DBL,LDBL}_MIN
 *   - {FLT,DBL,LDBL}_TRUE_MIN
 *
 * Drift discipline:
 *   - C11 §5.2.4.2.2¶8 requires `FLT_RADIX >= 2`, `*_MANT_DIG >= 1`,
 *     `*_DIG >= 6/10/10`, `*_MIN_10_EXP <= -37`, `*_MAX_10_EXP >= +37`,
 *     `FLT_EPSILON <= 1e-5`, etc. The host unit test pins each macro
 *     to the matching `__*__` compiler-predefined value AND to the
 *     standard-mandated minima, so a regression that drops or rewrites
 *     a macro fails the LINKED-libclib round-trip, not just the next
 *     preprocess.
 *   - `FLT_ROUNDS` is the only non-constant macro; the standard
 *     mandates it expand to an expression of type `int`, so we
 *     forward to a freestanding-safe `1` (round-to-nearest, the
 *     default IEEE-754 mode the kernel never reconfigures) when the
 *     compiler does not predefine `__FLT_ROUNDS__`. The host test
 *     pins the value but allows any of the standard `-1..3` range.
 *   - `FLT_EVAL_METHOD` and `FLT_HAS_SUBNORM`-family integer
 *     constants are required to be usable in `#if`; the helper TU
 *     re-folds each at its own compile time and the test compares.
 *   - No `OS_ABI_VERSION` bump: userland-only, additive header.
 *
 * References:
 *   - C11 §4¶6             (freestanding headers list)
 *   - C11 §5.2.4.2.2       (characteristics of floating types)
 *   - C11 §7.7             (<float.h>)
 *   - C11 §F.2             (IEC 60559 binding for binary floats)
 *   - C11 §7.1.3¶1         (reserved identifiers)
 */

#ifndef CLIB_FLOAT_H
#define CLIB_FLOAT_H

/* ---- radix + rounding mode + evaluation method (C11 §5.2.4.2.2¶8) -- */

#ifdef __FLT_RADIX__
#  define FLT_RADIX        __FLT_RADIX__
#else
#  define FLT_RADIX        2
#endif

/* §5.2.4.2.2¶8: FLT_ROUNDS is an expression of type int describing
 * the current rounding direction. Compilers do not generally fold
 * it. We forward when possible; otherwise default to "round to
 * nearest" (1), the IEEE-754 default the kernel never reconfigures. */
#ifdef __FLT_ROUNDS__
#  define FLT_ROUNDS       __FLT_ROUNDS__
#else
#  define FLT_ROUNDS       1
#endif

#ifdef __FLT_EVAL_METHOD__
#  define FLT_EVAL_METHOD  __FLT_EVAL_METHOD__
#else
#  define FLT_EVAL_METHOD  0
#endif

/* ---- subnormal-support indicators (C11 §5.2.4.2.2¶10) -------------- */

#ifdef __FLT_HAS_SUBNORM__
#  define FLT_HAS_SUBNORM   __FLT_HAS_SUBNORM__
#else
#  define FLT_HAS_SUBNORM   1
#endif
#ifdef __DBL_HAS_SUBNORM__
#  define DBL_HAS_SUBNORM   __DBL_HAS_SUBNORM__
#else
#  define DBL_HAS_SUBNORM   1
#endif
#ifdef __LDBL_HAS_SUBNORM__
#  define LDBL_HAS_SUBNORM  __LDBL_HAS_SUBNORM__
#else
#  define LDBL_HAS_SUBNORM  1
#endif

/* ---- DECIMAL_DIG (C11 §5.2.4.2.2¶11) ------------------------------- */

#ifdef __DECIMAL_DIG__
#  define DECIMAL_DIG       __DECIMAL_DIG__
#else
/* Annex F binding floor when the host doesn't predefine the macro:
 * 17 covers IEEE 754 double. */
#  define DECIMAL_DIG       17
#endif

/* ---- per-type mantissa / decimal-digit / exponent envelopes -------- */

/* mantissa-digits-in-radix-b */
#define FLT_MANT_DIG        __FLT_MANT_DIG__
#define DBL_MANT_DIG        __DBL_MANT_DIG__
#define LDBL_MANT_DIG       __LDBL_MANT_DIG__

/* decimal digits that survive a round-trip from a single FP value */
#define FLT_DECIMAL_DIG     __FLT_DECIMAL_DIG__
#define DBL_DECIMAL_DIG     __DBL_DECIMAL_DIG__
#define LDBL_DECIMAL_DIG    __LDBL_DECIMAL_DIG__

/* decimal digits guaranteed correct on round-trip */
#define FLT_DIG             __FLT_DIG__
#define DBL_DIG             __DBL_DIG__
#define LDBL_DIG            __LDBL_DIG__

/* minimum normalised exponent (base FLT_RADIX) */
#define FLT_MIN_EXP         __FLT_MIN_EXP__
#define DBL_MIN_EXP         __DBL_MIN_EXP__
#define LDBL_MIN_EXP        __LDBL_MIN_EXP__

/* minimum normalised exponent (base 10) */
#define FLT_MIN_10_EXP      __FLT_MIN_10_EXP__
#define DBL_MIN_10_EXP      __DBL_MIN_10_EXP__
#define LDBL_MIN_10_EXP     __LDBL_MIN_10_EXP__

/* maximum normalised exponent (base FLT_RADIX) */
#define FLT_MAX_EXP         __FLT_MAX_EXP__
#define DBL_MAX_EXP         __DBL_MAX_EXP__
#define LDBL_MAX_EXP        __LDBL_MAX_EXP__

/* maximum normalised exponent (base 10) */
#define FLT_MAX_10_EXP      __FLT_MAX_10_EXP__
#define DBL_MAX_10_EXP      __DBL_MAX_10_EXP__
#define LDBL_MAX_10_EXP     __LDBL_MAX_10_EXP__

/* finite-magnitude bounds + epsilon + smallest-normal + smallest-subnormal */
#define FLT_MAX             __FLT_MAX__
#define DBL_MAX             __DBL_MAX__
#define LDBL_MAX            __LDBL_MAX__

#define FLT_EPSILON         __FLT_EPSILON__
#define DBL_EPSILON         __DBL_EPSILON__
#define LDBL_EPSILON        __LDBL_EPSILON__

#define FLT_MIN             __FLT_MIN__
#define DBL_MIN             __DBL_MIN__
#define LDBL_MIN            __LDBL_MIN__

/* C11-only smallest-positive subnormal (Annex F.2). */
#ifdef __FLT_DENORM_MIN__
#  define FLT_TRUE_MIN      __FLT_DENORM_MIN__
#endif
#ifdef __DBL_DENORM_MIN__
#  define DBL_TRUE_MIN      __DBL_DENORM_MIN__
#endif
#ifdef __LDBL_DENORM_MIN__
#  define LDBL_TRUE_MIN     __LDBL_DENORM_MIN__
#endif

#endif /* CLIB_FLOAT_H */
