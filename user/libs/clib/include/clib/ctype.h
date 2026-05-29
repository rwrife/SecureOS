/*
 * include/clib/ctype.h
 * Freestanding userland ctype family (M7-TOOLCHAIN-004, issue #407).
 *
 * Purpose:
 *   Slice 2 of the in-OS toolchain freestanding libc (user/libs/clib,
 *   plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3). TinyCC's
 *   preprocessor and tokenizer (tccpp.c) call isdigit / isalpha /
 *   isalnum / isspace / isxdigit / isupper / islower / toupper /
 *   tolower extensively. None of these need syscalls or locale support,
 *   so they can land before stdio + setjmp and unblock that consumer
 *   immediately.
 *
 *   Slice 1 of #407 (the str/mem family in PR #416) is a peer of this
 *   slice; the two are independent and land in parallel -- different
 *   files, different symbol_set_pinned sub-marker.
 *
 * Containment:
 *   Freestanding. No libc, no kernel includes, no syscalls. Pure
 *   bit-twiddling on the 7-bit ASCII subset; behavior on int values
 *   outside [-1 (EOF), 0x00..0xFF] is undefined (matches ISO C).
 *
 * Symbol set (pinned by the symbol_set_pinned host-test sub-marker):
 *   Classification (each takes int, returns nonzero on match):
 *     - isascii, isdigit, isxdigit, isalpha, isalnum
 *     - isspace, isblank, isupper, islower
 *     - iscntrl, isprint, isgraph, ispunct
 *   Conversion:
 *     - toupper, tolower
 *
 * No locale, no _l variants, no UTF-8. Use canonical libc names so
 * TinyCC links unchanged.
 *
 * Interactions:
 *   - src/ctype.c               -- implementation (table-free, branch-thin).
 *   - tests/clib_ctype_test.c   -- host unit test, compiled with
 *                                  -fno-builtin so assertions exercise our
 *                                  implementations rather than __builtin_*.
 *   - build/scripts/test_clib_ctype.sh -- TEST:PASS:clib_ctype driver.
 *
 * Issue: 407. Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md (P3).
 */

#ifndef SECUREOS_USER_LIBS_CLIB_CTYPE_H
#define SECUREOS_USER_LIBS_CLIB_CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

int isascii(int c);
int isdigit(int c);
int isxdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isspace(int c);
int isblank(int c);
int isupper(int c);
int islower(int c);
int iscntrl(int c);
int isprint(int c);
int isgraph(int c);
int ispunct(int c);

int toupper(int c);
int tolower(int c);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_CTYPE_H */
