/*
 * src/stdlib.c
 * Freestanding stdlib subset (M7-TOOLCHAIN-004 slice 4, issue #407).
 *
 * Implementation notes:
 *   - Freestanding: no libc, no syscalls, no globals.
 *   - We reproduce just enough of <ctype.h> inline (the isspace test
 *     used by atoi/strtol/strtoul) so this translation unit has zero
 *     intra-clib dependencies — keeps the link of `libclib.a` flat and
 *     keeps the host unit test free of slice-1/slice-2 ordering.
 *   - Overflow uses unsigned accumulation so the cumulative check is
 *     well-defined in C; signed overflow is then translated by
 *     comparing against LONG_MAX / (LONG_MIN as unsigned).
 *   - abs / labs return the input unchanged on INT_MIN / LONG_MIN to
 *     avoid invoking signed-overflow UB; the C standard leaves this
 *     case undefined, so matching two's-complement HW is acceptable
 *     and avoids surprises in TinyCC's own driver.
 */

#include "../include/clib/stdlib.h"

#include <limits.h>
#include <stddef.h>

/* --- private isspace ---------------------------------------------------- */

static int s_isspace(int c) {
  /* Matches ISO C isspace for the 7-bit ASCII subset: SP, HT, LF, VT, FF, CR. */
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
         c == '\r';
}

/* --- digit value (or -1) ------------------------------------------------ */

static int s_digit_value(int c, int base) {
  int v;
  if (c >= '0' && c <= '9') {
    v = c - '0';
  } else if (c >= 'a' && c <= 'z') {
    v = 10 + (c - 'a');
  } else if (c >= 'A' && c <= 'Z') {
    v = 10 + (c - 'A');
  } else {
    return -1;
  }
  if (v >= base) {
    return -1;
  }
  return v;
}

/* --- core unsigned parse ------------------------------------------------ *
 *
 * Parses the digit run after sign/prefix stripping. Returns the parsed
 * value clamped to ULONG_MAX on overflow; sets *consumed_any to nonzero
 * iff at least one digit was consumed; advances *p past the run.
 */
static unsigned long s_parse_digits(const char **p, int base,
                                    int *consumed_any, int *overflow) {
  const char   *q     = *p;
  unsigned long acc   = 0;
  int           any   = 0;
  int           over  = 0;

  for (;; q++) {
    int v = s_digit_value((unsigned char)*q, base);
    if (v < 0) {
      break;
    }
    any = 1;

    /* Overflow check: acc * base + v > ULONG_MAX ? */
    unsigned long limit_div = ULONG_MAX / (unsigned long)base;
    unsigned long limit_mod = ULONG_MAX % (unsigned long)base;
    if (acc > limit_div ||
        (acc == limit_div && (unsigned long)v > limit_mod)) {
      over = 1;
      acc  = ULONG_MAX;
      /* Continue scanning digits so *endptr lands past the entire run,
       * matching the C99 contract that consumers like TinyCC rely on. */
      continue;
    }
    acc = acc * (unsigned long)base + (unsigned long)v;
  }

  *p             = q;
  *consumed_any  = any;
  *overflow      = over;
  return acc;
}

/* --- shared prefix handling --------------------------------------------- *
 *
 * Skip leading spaces, consume optional sign, resolve base from prefix
 * if base==0, consume optional "0x" if base==16. Returns the sign
 * (1 or -1) and updates *p / *base in place.
 */
static int s_strip_prefix(const char **p, int *base) {
  const char *q    = *p;
  int         sign = 1;

  while (s_isspace((unsigned char)*q)) {
    q++;
  }
  if (*q == '+') {
    q++;
  } else if (*q == '-') {
    sign = -1;
    q++;
  }

  if (*base == 0) {
    if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X') &&
        s_digit_value((unsigned char)q[2], 16) >= 0) {
      *base = 16;
      q += 2;
    } else if (q[0] == '0') {
      *base = 8;
      /* Do NOT advance past the leading '0'; if no further octal digits
       * follow, the digit run will still consume that '0' and yield 0. */
    } else {
      *base = 10;
    }
  } else if (*base == 16) {
    if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X') &&
        s_digit_value((unsigned char)q[2], 16) >= 0) {
      q += 2;
    }
  }

  *p = q;
  return sign;
}

/* --- public API --------------------------------------------------------- */

long strtol(const char *nptr, char **endptr, int base) {
  const char *p              = nptr;
  int         consumed_any   = 0;
  int         overflow_flag  = 0;

  if (base != 0 && (base < 2 || base > 36)) {
    if (endptr) {
      *endptr = (char *)nptr;
    }
    return 0;
  }

  int sign = s_strip_prefix(&p, &base);

  const char *digit_start = p;
  unsigned long mag =
      s_parse_digits(&p, base, &consumed_any, &overflow_flag);

  if (!consumed_any) {
    /* Per C99 7.20.1.4: if no conversion can be performed, *endptr is
     * set to nptr (the original pointer, not the post-sign one). */
    if (endptr) {
      *endptr = (char *)nptr;
    }
    (void)digit_start;
    return 0;
  }

  if (endptr) {
    *endptr = (char *)p;
  }

  /* Signed clamp. LONG_MIN_AS_ULONG = (unsigned long)LONG_MAX + 1. */
  const unsigned long long_min_mag = (unsigned long)LONG_MAX + 1UL;

  if (sign > 0) {
    if (overflow_flag || mag > (unsigned long)LONG_MAX) {
      return LONG_MAX;
    }
    return (long)mag;
  } else {
    if (overflow_flag || mag > long_min_mag) {
      return LONG_MIN;
    }
    if (mag == long_min_mag) {
      return LONG_MIN;
    }
    return -(long)mag;
  }
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
  const char *p              = nptr;
  int         consumed_any   = 0;
  int         overflow_flag  = 0;

  if (base != 0 && (base < 2 || base > 36)) {
    if (endptr) {
      *endptr = (char *)nptr;
    }
    return 0;
  }

  int sign = s_strip_prefix(&p, &base);

  unsigned long mag =
      s_parse_digits(&p, base, &consumed_any, &overflow_flag);

  if (!consumed_any) {
    if (endptr) {
      *endptr = (char *)nptr;
    }
    return 0;
  }

  if (endptr) {
    *endptr = (char *)p;
  }

  if (overflow_flag) {
    return ULONG_MAX;
  }

  if (sign < 0) {
    /* C standard: negation modulo ULONG_MAX+1. 0 stays 0. */
    return (unsigned long)0 - mag;
  }
  return mag;
}

int atoi(const char *s) {
  /* Per C99 7.20.1.1, atoi is equivalent to (int)strtol(s, NULL, 10)
   * except the overflow behavior is undefined. We route through
   * strtol for shared parse, then narrow. */
  long v = strtol(s, NULL, 10);
  if (v > INT_MAX) {
    return INT_MAX;
  }
  if (v < INT_MIN) {
    return INT_MIN;
  }
  return (int)v;
}

int abs(int x) {
  if (x == INT_MIN) {
    /* Avoid signed-overflow UB; return as-is. */
    return x;
  }
  return x < 0 ? -x : x;
}

long labs(long x) {
  if (x == LONG_MIN) {
    return x;
  }
  return x < 0 ? -x : x;
}
