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
