/**
 * @file secureos_compat.c
 * @brief Freestanding libc shims required by BearSSL.
 *
 * Purpose:
 *   BearSSL calls five standard C library functions: memcpy, memmove,
 *   memset, memcmp, and strlen.  This file provides minimal freestanding
 *   implementations so BearSSL compiles and links without libc.
 *
 * Interactions:
 *   - Linked into any target that includes BearSSL objects (kernel image
 *     and the standalone netlib shared library).
 *
 * Launched by:
 *   Not standalone; compiled alongside BearSSL objects.
 */

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  size_t i;

  for (i = 0u; i < n; ++i) {
    d[i] = s[i];
  }
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  size_t i;

  if (d < s) {
    for (i = 0u; i < n; ++i) {
      d[i] = s[i];
    }
  } else if (d > s) {
    for (i = n; i > 0u; --i) {
      d[i - 1u] = s[i - 1u];
    }
  }
  return dst;
}

void *memset(void *s, int c, size_t n) {
  uint8_t *p = (uint8_t *)s;
  size_t i;

  for (i = 0u; i < n; ++i) {
    p[i] = (uint8_t)c;
  }
  return s;
}

int memcmp(const void *a, const void *b, size_t n) {
  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;
  size_t i;

  for (i = 0u; i < n; ++i) {
    if (pa[i] != pb[i]) {
      return (int)pa[i] - (int)pb[i];
    }
  }
  return 0;
}

size_t strlen(const char *s) {
  size_t n = 0u;

  if (s == 0) {
    return 0u;
  }
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}