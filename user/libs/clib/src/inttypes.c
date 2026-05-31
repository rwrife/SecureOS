/**
 * @file src/inttypes.c
 * @brief Drift-anchor helper for the freestanding <inttypes.h>
 *        format-string nucleus (issue #407 slice 11, M7-TOOLCHAIN-004).
 *
 * The header is pure preprocessor; this TU exists so the unit test
 * can observe the same format-string macro values the LINKED libclib
 * observes, not just what the test TU's #include happened to see at
 * its own compile time.
 *
 * Mirrors the drift-anchor pattern used by src/stdint.c (PR #437),
 * src/stddef.c (PR #436), and src/limits.c (PR #434).
 */

#include "../include/clib/inttypes.h"
#include "../include/clib/stdint.h"
#include "../include/clib/stdlib.h"

/* Width contract: the strtoimax/strtoumax implementations forward to
 * strtoll/strtoull (slice 9 extension, PR #444 / #458). That forward
 * is only safe when intmax_t/uintmax_t are layout-compatible with
 * long long / unsigned long long. Pin loudly at build time so a
 * future target with a wider intmax_t doesn't silently truncate. */
_Static_assert(sizeof(intmax_t)  == sizeof(long long),
               "clib_inttypes: intmax_t must be layout-compatible with long long");
_Static_assert(sizeof(uintmax_t) == sizeof(unsigned long long),
               "clib_inttypes: uintmax_t must be layout-compatible with unsigned long long");

const char *clib_inttypes_fmt(int which) {
  switch (which) {
    case CLIB_INTTYPES_SEL_PRId8:    return PRId8;
    case CLIB_INTTYPES_SEL_PRId16:   return PRId16;
    case CLIB_INTTYPES_SEL_PRId32:   return PRId32;
    case CLIB_INTTYPES_SEL_PRId64:   return PRId64;
    case CLIB_INTTYPES_SEL_PRIu32:   return PRIu32;
    case CLIB_INTTYPES_SEL_PRIu64:   return PRIu64;
    case CLIB_INTTYPES_SEL_PRIx32:   return PRIx32;
    case CLIB_INTTYPES_SEL_PRIx64:   return PRIx64;
    case CLIB_INTTYPES_SEL_PRIdMAX:  return PRIdMAX;
    case CLIB_INTTYPES_SEL_PRIuMAX:  return PRIuMAX;
    case CLIB_INTTYPES_SEL_PRIdPTR:  return PRIdPTR;
    case CLIB_INTTYPES_SEL_PRIuPTR:  return PRIuPTR;
    case CLIB_INTTYPES_SEL_SCNd32:   return SCNd32;
    case CLIB_INTTYPES_SEL_SCNu64:   return SCNu64;
    default:                         return (const char *)0;
  }
}

/* ---- C11 §7.8.2 imaxabs / imaxdiv ----------------------------------- */

intmax_t imaxabs(intmax_t j) {
  if (j == INTMAX_MIN) {
    /* Negation is UB per C11 §7.8.2.1; mirror abs/labs's two's-complement
     * carve-out and return the input unchanged. */
    return j;
  }
  return j < 0 ? -j : j;
}

imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom) {
  imaxdiv_t out;
  if (denom == 0) {
    /* C11 §7.8.2.2 leaves divide-by-zero behavior undefined. Mirror the
     * deny-clean discipline used elsewhere in clib: never trap, surface
     * a sentinel quot and rem == 0 instead. */
    out.quot = numer >= 0 ? INTMAX_MAX : INTMAX_MIN;
    out.rem  = 0;
    return out;
  }
  /* C11 §6.5.5p6: integer division truncates toward zero; rem has the
   * sign of numer when rem != 0. Native /,% already satisfy this on a
   * conforming C99+ compiler, which is the only configuration libclib
   * targets. */
  out.quot = numer / denom;
  out.rem  = numer % denom;
  return out;
}

/* ---- C11 §7.8.2 strtoimax / strtoumax ------------------------------- */

intmax_t strtoimax(const char *nptr, char **endptr, int base) {
  return (intmax_t)strtoll(nptr, endptr, base);
}

uintmax_t strtoumax(const char *nptr, char **endptr, int base) {
  return (uintmax_t)strtoull(nptr, endptr, base);
}
