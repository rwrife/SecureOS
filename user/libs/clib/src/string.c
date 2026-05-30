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

/* --- string tokenize / span -------------------------------------------- *
 *
 * Slice 12 of M7-TOOLCHAIN-004 (issue #407). Adds the freestanding
 * tokenize / span family TinyCC's argv + include-path parsers link
 * against. Pure byte-level, no allocator, no syscalls.
 *
 * Implementation notes:
 *   - Class membership (`s_in_set`) treats bytes as `unsigned char`,
 *     mirroring the existing `memcmp` posture so that ordering and
 *     membership match the C standard regardless of host `char`
 *     signedness.
 *   - `strtok` uses a single static `char *` for state (canonical
 *     C99 §7.21.5.8). The re-entrant `strtok_r` keeps its state in
 *     a caller-provided `*saveptr` and is implemented by the same
 *     core helper so the two cannot drift apart.
 *   - `strpbrk` returns the first occurrence of any byte from
 *     `accept` in `s`, or NULL on no match. `strspn` returns the
 *     length of the leading run of bytes that ARE in `accept`;
 *     `strcspn` returns the leading run that are NOT in `reject`.
 *     All three treat NUL as a hard terminator on the searched
 *     string (`s`) but a normal byte inside the set string when
 *     scanning the set — i.e. the set ends at its own NUL exactly as
 *     the standard prescribes.
 *   - Defensive posture: NULL `s` or NULL set inputs are treated as
 *     empty rather than dereferenced, matching the same
 *     no-crash-on-programmer-mistake stance used by qsort / bsearch
 *     slices.
 */

static int s_in_set(unsigned char c, const char *set) {
  if (set == (const char *)0) {
    return 0;
  }
  for (const unsigned char *p = (const unsigned char *)set; *p != 0; p++) {
    if (*p == c) {
      return 1;
    }
  }
  return 0;
}

size_t strspn(const char *s, const char *accept) {
  if (s == (const char *)0) {
    return 0;
  }
  size_t i = 0;
  while (s[i] != '\0' && s_in_set((unsigned char)s[i], accept)) {
    i++;
  }
  return i;
}

size_t strcspn(const char *s, const char *reject) {
  if (s == (const char *)0) {
    return 0;
  }
  size_t i = 0;
  while (s[i] != '\0' && !s_in_set((unsigned char)s[i], reject)) {
    i++;
  }
  return i;
}

char *strpbrk(const char *s, const char *accept) {
  if (s == (const char *)0) {
    return (char *)0;
  }
  for (size_t i = 0; s[i] != '\0'; i++) {
    if (s_in_set((unsigned char)s[i], accept)) {
      return (char *)(s + i);
    }
  }
  return (char *)0;
}

char *strtok_r(char *s, const char *delim, char **saveptr) {
  if (saveptr == (char **)0) {
    return (char *)0;
  }
  char *cursor = (s != (char *)0) ? s : *saveptr;
  if (cursor == (char *)0) {
    return (char *)0;
  }
  /* Skip leading delimiters. */
  while (*cursor != '\0' && s_in_set((unsigned char)*cursor, delim)) {
    cursor++;
  }
  if (*cursor == '\0') {
    *saveptr = cursor;
    return (char *)0;
  }
  /* Scan to the next delimiter (or end of string). */
  char *tok_start = cursor;
  while (*cursor != '\0' && !s_in_set((unsigned char)*cursor, delim)) {
    cursor++;
  }
  if (*cursor == '\0') {
    *saveptr = cursor;
  } else {
    *cursor  = '\0';
    *saveptr = cursor + 1;
  }
  return tok_start;
}

/* Canonical C99 `strtok` keeps its state in a single static pointer.
 * Not thread-safe by design — the in-OS toolchain is single-threaded
 * (plan P0 §Threading model). */
static char *s_strtok_saveptr;

char *strtok(char *s, const char *delim) {
  return strtok_r(s, delim, &s_strtok_saveptr);
}
