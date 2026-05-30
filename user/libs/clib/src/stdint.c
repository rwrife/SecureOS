/**
 * @file src/stdint.c
 * @brief Drift-anchor helpers for the freestanding <stdint.h> nucleus
 *        (issue #407 slice 10, M7-TOOLCHAIN-004).
 *
 * The header (`include/clib/stdint.h`) defines the typedefs and limit
 * macros; this TU exists so the unit test can observe the same widths
 * and limits the LINKED libclib observes, not just what the test TU's
 * #include happened to see at its own compile time.
 *
 * Mirrors the pattern used by `src/stddef.c` (PR #436) and
 * `src/limits.c` (PR #434).
 */

#include "../include/clib/stdint.h"

unsigned long clib_stdint_sizeof(int which) {
  switch (which) {
    case CLIB_STDINT_SIZE_INT8:    return (unsigned long)sizeof(int8_t);
    case CLIB_STDINT_SIZE_INT16:   return (unsigned long)sizeof(int16_t);
    case CLIB_STDINT_SIZE_INT32:   return (unsigned long)sizeof(int32_t);
    case CLIB_STDINT_SIZE_INT64:   return (unsigned long)sizeof(int64_t);
    case CLIB_STDINT_SIZE_UINT8:   return (unsigned long)sizeof(uint8_t);
    case CLIB_STDINT_SIZE_UINT16:  return (unsigned long)sizeof(uint16_t);
    case CLIB_STDINT_SIZE_UINT32:  return (unsigned long)sizeof(uint32_t);
    case CLIB_STDINT_SIZE_UINT64:  return (unsigned long)sizeof(uint64_t);
    case CLIB_STDINT_SIZE_INTPTR:  return (unsigned long)sizeof(intptr_t);
    case CLIB_STDINT_SIZE_UINTPTR: return (unsigned long)sizeof(uintptr_t);
    case CLIB_STDINT_SIZE_INTMAX:  return (unsigned long)sizeof(intmax_t);
    case CLIB_STDINT_SIZE_UINTMAX: return (unsigned long)sizeof(uintmax_t);
    default:                       return 0u;
  }
}

unsigned long long clib_stdint_maxof(int which) {
  switch (which) {
    case CLIB_STDINT_SIZE_INT8:    return (unsigned long long)INT8_MAX;
    case CLIB_STDINT_SIZE_INT16:   return (unsigned long long)INT16_MAX;
    case CLIB_STDINT_SIZE_INT32:   return (unsigned long long)INT32_MAX;
    case CLIB_STDINT_SIZE_INT64:   return (unsigned long long)INT64_MAX;
    case CLIB_STDINT_SIZE_UINT8:   return (unsigned long long)UINT8_MAX;
    case CLIB_STDINT_SIZE_UINT16:  return (unsigned long long)UINT16_MAX;
    case CLIB_STDINT_SIZE_UINT32:  return (unsigned long long)UINT32_MAX;
    case CLIB_STDINT_SIZE_UINT64:  return (unsigned long long)UINT64_MAX;
    case CLIB_STDINT_SIZE_INTPTR:  return (unsigned long long)INTPTR_MAX;
    case CLIB_STDINT_SIZE_UINTPTR: return (unsigned long long)UINTPTR_MAX;
    case CLIB_STDINT_SIZE_INTMAX:  return (unsigned long long)INTMAX_MAX;
    case CLIB_STDINT_SIZE_UINTMAX: return (unsigned long long)UINTMAX_MAX;
    default:                       return (unsigned long long)-1;
  }
}
