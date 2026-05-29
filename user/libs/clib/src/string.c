/**
 * @file src/string.c
 * @brief Freestanding string / memory family (M7-TOOLCHAIN-004 slice 1, issue #407).
 *
 * Implementation notes:
 *   - Every routine is freestanding: no libc, no syscalls, no globals.
 *   - `memmove` handles overlapping copies in either direction.
 *   - `strncpy` is the historical "pad with NULs up to n" form (the
 *     standard behaviour TinyCC and other consumers expect).
 *   - All comparisons treat bytes as `unsigned char` so that ordering
 *     matches the C standard regardless of host `char` signedness.
 *   - We deliberately do NOT delegate to host `__builtin_*` so that
 *     the host unit test (compiled with `-fno-builtin`) exercises this
 *     code path even though the same binary runs on a hosted compiler.
 */

#include "../include/clib/string.h"

#include <stdint.h>

/* --- memory family ------------------------------------------------------ */

void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char       *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  for (size_t i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char       *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  if (d == s || n == 0) {
    return dst;
  }
  /* If dst is below src, or the regions do not overlap, a forward copy
   * is safe. Otherwise copy backwards so we do not overwrite source
   * bytes before reading them. */
  if (d < s || (uintptr_t)(d - s) >= n) {
    for (size_t i = 0; i < n; i++) {
      d[i] = s[i];
    }
  } else {
    for (size_t i = n; i > 0; i--) {
      d[i - 1] = s[i - 1];
    }
  }
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  unsigned char  v = (unsigned char)c;
  for (size_t i = 0; i < n; i++) {
    d[i] = v;
  }
  return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  for (size_t i = 0; i < n; i++) {
    if (pa[i] != pb[i]) {
      return (int)pa[i] - (int)pb[i];
    }
  }
  return 0;
}

void *memchr(const void *s, int c, size_t n) {
  const unsigned char *p = (const unsigned char *)s;
  unsigned char        v = (unsigned char)c;
  for (size_t i = 0; i < n; i++) {
    if (p[i] == v) {
      return (void *)(p + i);
    }
  }
  return (void *)0;
}

/* --- string length ------------------------------------------------------ */

size_t strlen(const char *s) {
  size_t i = 0;
  while (s[i] != '\0') {
    i++;
  }
  return i;
}

size_t strnlen(const char *s, size_t max) {
  size_t i = 0;
  while (i < max && s[i] != '\0') {
    i++;
  }
  return i;
}

/* --- string compare ----------------------------------------------------- */

int strcmp(const char *a, const char *b) {
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  while (*pa != '\0' && *pa == *pb) {
    pa++;
    pb++;
  }
  return (int)*pa - (int)*pb;
}

int strncmp(const char *a, const char *b, size_t n) {
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  for (size_t i = 0; i < n; i++) {
    unsigned char ca = pa[i];
    unsigned char cb = pb[i];
    if (ca != cb) {
      return (int)ca - (int)cb;
    }
    if (ca == '\0') {
      return 0;
    }
  }
  return 0;
}

/* --- string copy -------------------------------------------------------- */

char *strcpy(char *dst, const char *src) {
  char *d = dst;
  while ((*d++ = *src++) != '\0') {
  }
  return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
  size_t i = 0;
  for (; i < n && src[i] != '\0'; i++) {
    dst[i] = src[i];
  }
  for (; i < n; i++) {
    dst[i] = '\0';
  }
  return dst;
}

/* --- string concat ------------------------------------------------------ */

char *strcat(char *dst, const char *src) {
  char *d = dst;
  while (*d != '\0') {
    d++;
  }
  while ((*d++ = *src++) != '\0') {
  }
  return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
  char *d = dst;
  while (*d != '\0') {
    d++;
  }
  for (size_t i = 0; i < n && src[i] != '\0'; i++) {
    *d++ = src[i];
  }
  *d = '\0';
  return dst;
}

/* --- string search ------------------------------------------------------ */

char *strchr(const char *s, int c) {
  char target = (char)c;
  for (;;) {
    if (*s == target) {
      return (char *)s;
    }
    if (*s == '\0') {
      return (char *)0;
    }
    s++;
  }
}

char *strrchr(const char *s, int c) {
  char        target = (char)c;
  const char *last   = (const char *)0;
  for (;;) {
    if (*s == target) {
      last = s;
    }
    if (*s == '\0') {
      return (char *)last;
    }
    s++;
  }
}

char *strstr(const char *haystack, const char *needle) {
  if (needle[0] == '\0') {
    return (char *)haystack;
  }
  for (size_t i = 0; haystack[i] != '\0'; i++) {
    size_t j = 0;
    while (needle[j] != '\0' && haystack[i + j] == needle[j]) {
      j++;
    }
    if (needle[j] == '\0') {
      return (char *)(haystack + i);
    }
    if (haystack[i + j] == '\0') {
      return (char *)0;
    }
  }
  return (char *)0;
}
