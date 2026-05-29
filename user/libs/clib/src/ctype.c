/**
 * @file src/ctype.c
 * @brief Freestanding ctype family for user/libs/clib (issue #407 slice 2).
 *
 * Implementation notes:
 *   - All predicates accept the standard `int` argument and treat values
 *     outside `[-1, 0xFF]` as "no" (returning 0). EOF (`-1`) is also "no".
 *   - No lookup table: the freestanding image stays a few hundred bytes
 *     smaller, and the branch-thin form is small enough that there is no
 *     measurable speed difference for TinyCC's lexer.
 *   - toupper / tolower are pure case flips on the ASCII letter range; any
 *     other input is returned unchanged (ISO C semantics).
 *   - No locale. SecureOS userland is ASCII-only at OS_ABI_VERSION=0.
 *
 * No dependency on libc or syscalls. Compiles under -ffreestanding.
 */

#include "../include/clib/ctype.h"

int isascii(int c) {
  return ((unsigned)c) < 128u;
}

int isdigit(int c) {
  return c >= '0' && c <= '9';
}

int isxdigit(int c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

int isupper(int c) {
  return c >= 'A' && c <= 'Z';
}

int islower(int c) {
  return c >= 'a' && c <= 'z';
}

int isalpha(int c) {
  return isupper(c) || islower(c);
}

int isalnum(int c) {
  return isalpha(c) || isdigit(c);
}

int isblank(int c) {
  return c == ' ' || c == '\t';
}

int isspace(int c) {
  /* space, \t, \n, \v, \f, \r — exactly the ISO C set. */
  return c == ' ' || (c >= '\t' && c <= '\r');
}

int iscntrl(int c) {
  return (c >= 0 && c <= 0x1F) || c == 0x7F;
}

int isprint(int c) {
  return c >= 0x20 && c <= 0x7E;
}

int isgraph(int c) {
  /* isprint minus space. */
  return c > 0x20 && c <= 0x7E;
}

int ispunct(int c) {
  /* Printable, not space, not alnum. */
  return isgraph(c) && !isalnum(c);
}

int toupper(int c) {
  if (c >= 'a' && c <= 'z') {
    return c - ('a' - 'A');
  }
  return c;
}

int tolower(int c) {
  if (c >= 'A' && c <= 'Z') {
    return c + ('a' - 'A');
  }
  return c;
}
