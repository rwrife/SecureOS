/**
 * @file string.h
 * @brief Freestanding string.h shim for BearSSL.
 *
 * Declares the subset of string/memory functions that BearSSL requires.
 * Implementations are in vendor/bearssl/secureos_compat.c.
 */
#ifndef _SECUREOS_STRING_H
#define _SECUREOS_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

#endif /* _SECUREOS_STRING_H */
