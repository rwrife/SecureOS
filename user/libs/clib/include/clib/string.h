/**
 * @file include/clib/string.h
 * @brief Freestanding string / memory family for user/libs/clib
 *        (M7-TOOLCHAIN-004 slice 1, issue #407).
 *
 * Purpose:
 *   Slice 1 of M7-TOOLCHAIN-004 (`user/libs/clib` freestanding libc subset,
 *   plan `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3). TinyCC and
 *   other in-OS toolchain consumers link against the standard string /
 *   memory family. The allocator landed in slice 1 of #404; this slice
 *   adds the deterministic, freestanding `str*`/`mem*` symbols that have
 *   no syscall dependency and can therefore land before stdio / setjmp.
 *
 * Containment:
 *   - Freestanding. No libc, no kernel includes, no syscalls.
 *   - Pure byte-level pointer arithmetic and comparisons. Safe to link
 *     into the on-target SDK runtime once the build wires up
 *     `artifacts/sdk/libclib.a` in the M7-TOOLCHAIN-001 kernel-side
 *     follow-up.
 *
 * Symbol coverage (slice 1):
 *   - memcpy, memmove, memset, memcmp, memchr
 *   - strlen, strnlen
 *   - strcmp, strncmp
 *   - strcpy, strncpy
 *   - strcat, strncat
 *   - strchr, strrchr
 *   - strstr
 *
 * Symbol coverage (slice 12, additive, issue #407):
 *   - strspn, strcspn, strpbrk
 *   - strtok, strtok_r
 *
 * Out of scope for this slice (folded in by later #407 slices once
 * TinyCC's link-error set pins them):
 *   - stdio (`fopen` / `fprintf` / `fread` / `fwrite` / `fclose`) — needs
 *     M7-TOOLCHAIN-002 file-I/O size + os_console_write wiring.
 *   - setjmp / longjmp — arch-specific, needs the on-target build.
 *   - qsort — added once a TinyCC drop forces it.
 *   - locale, threads, signals — explicit non-goals per the issue body.
 *
 * Naming:
 *   Symbols use the canonical libc names (`memcpy`, `strlen`, ...) so
 *   that TinyCC and other consumers link "for free" — no rename shim
 *   required.  The host unit test compiles this file with
 *   `-fno-builtin` so the test exercises **our** implementations rather
 *   than the host libc's `__builtin_memcpy` etc.
 *
 * ABI status:
 *   Userland-only. Does **not** bump `OS_ABI_VERSION` (parity with
 *   `clib_malloc`).
 *
 * Issue: #407. Refs umbrella #403. Plan: P3 in
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md`.
 */

#ifndef SECUREOS_USER_LIBS_CLIB_STRING_H
#define SECUREOS_USER_LIBS_CLIB_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- memory family ------------------------------------------------------ */

void   *memcpy (void *dst, const void *src, size_t n);
void   *memmove(void *dst, const void *src, size_t n);
void   *memset (void *dst, int c, size_t n);
int     memcmp (const void *a, const void *b, size_t n);
void   *memchr (const void *s, int c, size_t n);

/* --- string length ------------------------------------------------------ */

size_t  strlen (const char *s);
size_t  strnlen(const char *s, size_t max);

/* --- string compare ----------------------------------------------------- */

int     strcmp (const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t n);

/* --- string copy -------------------------------------------------------- */

char   *strcpy (char *dst, const char *src);
char   *strncpy(char *dst, const char *src, size_t n);

/* --- string concat ------------------------------------------------------ */

char   *strcat (char *dst, const char *src);
char   *strncat(char *dst, const char *src, size_t n);

/* --- string search ------------------------------------------------------ */

char   *strchr (const char *s, int c);
char   *strrchr(const char *s, int c);
char   *strstr (const char *haystack, const char *needle);

/* --- string tokenize / span -------------------------------------------- *
 *
 * M7-TOOLCHAIN-004 slice 12 (issue #407): freestanding tokenize / span
 * family. TinyCC's option parser (`tcc.c` argv walk), include-path /
 * library-path splitters, and `-D`/`-U` macro definition parser all
 * link against `strspn`, `strcspn`, `strpbrk`, and the `strtok` /
 * `strtok_r` pair.
 *
 * Notes:
 *   - `strtok` keeps state in a single static `char *` (canonical C99
 *     §7.21.5.8 contract). Not thread-safe by design — the in-OS
 *     toolchain is single-threaded (P0 plan §"Threading model"), so
 *     this matches the consumer.
 *   - `strtok_r` is the re-entrant POSIX variant that threads its
 *     state through a caller-provided `char **saveptr`. We ship it
 *     because some TinyCC drops (and any future on-target callers)
 *     prefer the re-entrant form and the implementation cost is one
 *     extra public symbol over the canonical `strtok`.
 */

size_t  strspn (const char *s, const char *accept);
size_t  strcspn(const char *s, const char *reject);
char   *strpbrk(const char *s, const char *accept);
char   *strtok (char *s, const char *delim);
char   *strtok_r(char *s, const char *delim, char **saveptr);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_STRING_H */
