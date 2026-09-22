/*
 * vendor/tinycc/include/math.h — freestanding <math.h> shim for the
 * SecureOS TinyCC build (issue #766 / #408 Phase 3, first-compile slice).
 *
 * TinyCC's tcc.h includes <math.h> unconditionally, but the in-scope
 * `TCC_ALL_SRCS` set calls exactly one math function: `ldexpl()` from
 * `tccpp.c` (decimal float-literal scaling). clib has no float surface
 * yet, so this shim carries the declaration only — sufficient to
 * COMPILE the compiler. The eventual link step (#766 follow-up) must
 * provide a deterministic freestanding `ldexpl`; until then any link
 * that reaches the float-literal path fails loudly on the undefined
 * symbol rather than silently miscompiling.
 *
 * The declaration is guarded by CLIB_FREESTANDING_NO_LIBM_DEF so host
 * gates that also see a hosted <math.h> cannot get a redefinition.
 * Discovered through `-I vendor/tinycc/include` with `-nostdlibinc`.
 */

#ifndef SECUREOS_TINYCC_SHIM_MATH_H
#define SECUREOS_TINYCC_SHIM_MATH_H

#ifndef CLIB_FREESTANDING_NO_LIBM_DEF
#define CLIB_FREESTANDING_NO_LIBM_DEF 1
long double ldexpl(long double x, int exp);
#endif

#endif /* SECUREOS_TINYCC_SHIM_MATH_H */
