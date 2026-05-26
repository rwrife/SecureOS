/**
 * @file sosh_compat.c
 * @brief Freestanding libc shims required by the sosh interpreter.
 *
 * Purpose:
 *   The compiler may emit implicit calls to memset (e.g., for zeroing
 *   large local structs). This file provides a minimal freestanding
 *   implementation so soshlib links without libc.
 *
 * Interactions:
 *   - Linked into the soshlib standalone library binary.
 *   - Also used when sosh app is built (via #include of .c files).
 *
 * Launched by:
 *   Not standalone; compiled alongside soshlib objects.
 */

#include <stddef.h>
#include <stdint.h>

void *memset(void *s, int c, size_t n) {
  uint8_t *p = (uint8_t *)s;
  size_t i;
  for (i = 0u; i < n; ++i) {
    p[i] = (uint8_t)c;
  }
  return s;
}

void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  size_t i;
  for (i = 0u; i < n; ++i) {
    d[i] = s[i];
  }
  return dst;
}
