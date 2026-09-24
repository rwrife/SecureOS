/**
 * @file src/strtod.c
 * @brief Freestanding `strtod` / `strtof` / `strtold` / `ldexpl`
 *        decimal->binary conversion (M7 toolchain surface, issue #766).
 *
 * Purpose:
 *   TinyCC's preprocessor folds floating-point literals at preprocess
 *   time (`tccpp.c` calls `strtod`/`strtof`/`strtold` for decimal
 *   literals and `ldexpl` for `p`-exponent hex literals). clib carried
 *   none of these, so the in-OS libtcc link surfaced them as undefined
 *   (issue #766, DEMO-02).
 *
 * Algorithm (deliberately simple, portable, no libm):
 *   Decimal literals are accumulated into a `long double` with
 *   digit-at-a-time multiply-add (`d = d*10 + digit`), then scaled by
 *   a power-of-ten square-and-multiply loop for the fraction/exponent.
 *   `ldexpl` scales by a binary exponent the same way.
 *
 *   Accuracy is within a couple of ulps of a correctly-rounded
 *   implementation: digit-stream accumulation can round more than once.
 *   That is acceptable for compiler constant folding in the in-OS demo
 *   toolchain (source constants are short), and it is deterministic —
 *   same input, same bits, every run and every host, which is what the
 *   golden-comparison gates need. It is NOT a correctly-rounded libc
 *   `strtod`; do not advertise it as one.
 *
 * Interactions:
 *   Called by TinyCC's `tccpp.c` float-literal folding, compiled by
 *   build/scripts/build_tinycc.sh (issue #766). TinyCC sees the
 *   declarations through its own tcc.h externs; host consumers see
 *   clib's include/clib/stdlib.h (extended by this slice).
 *
 * Containment:
 *   Freestanding. Pure C11 over <stddef.h>; no syscalls, no libc,
 *   no libm, no locale.
 */

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* 10^n for n in [0, ~400] via square-and-multiply. */
static long double clib_pow10l(unsigned int n) {
  long double base = 10.0L;
  long double acc = 1.0L;
  while (n != 0u) {
    if (n & 1u) {
      acc *= base;
    }
    base *= base;
    n >>= 1;
  }
  return acc;
}

/* base^e for base == 2 (or 0.5) via square-and-multiply. */
static long double clib_pow2l(int n) {
  long double base = (n < 0) ? 0.5L : 2.0L;
  long double acc = 1.0L;
  unsigned int e = (n < 0) ? (unsigned int)(-(n + 1)) + 1u : (unsigned int)n;
  while (e != 0u) {
    if (e & 1u) {
      acc *= base;
    }
    base *= base;
    e >>= 1;
  }
  return acc;
}

/* Skip leading whitespace per C11 §7.22.1.3. */
static const char *clib_skip_ws(const char *s) {
  while (*s == ' ' || (*s >= '\t' && *s <= '\r')) {
    ++s;
  }
  return s;
}

/* Parse one digit with hex value d (0..15) if `c` qualifies; else -1. */
static int clib_hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* Parse a signed decimal exponent at *ps; apply scale via *scalefn.
 * Returns 1 and advances *ps when a well-formed exponent was consumed. */
static int clib_read_exp(const char **ps, int *exp_out) {
  const char *s = *ps;
  int neg = 0, ev = 0, any = 0;
  if (*s == '+' || *s == '-') {
    neg = (*s == '-');
    ++s;
  }
  while (*s >= '0' && *s <= '9') {
    ev = ev * 10 + (*s - '0');
    if (ev > 100000) ev = 100000; /* saturate: outcome is 0 or inf */
    ++s;
    any = 1;
  }
  if (!any) {
    return 0;
  }
  *ps = s;
  *exp_out = neg ? -ev : ev;
  return 1;
}

/*
 * Core parser. Returns the parsed value and sets *end (when non-NULL)
 * to the first byte not consumed. Accepts:
 *   [ws] [+|-] digits[.digits] [eE [+-] digits]
 *   [ws] [+|-] 0[xX] hexdigits[.hexdigits] [pP [+-] digits]
 *   [ws] [+|-] inf[inity] | nan[('(chars)')]
 * Trailing junk (e.g. the float suffix 'f'/'l', which TinyCC strips
 * itself) simply terminates the scan, matching strtod semantics.
 * *bad is set when no conversion could be performed.
 */
static long double clib_strtopd(const char *np, char **end, int *bad) {
  const char *s = clib_skip_ws(np);
  const char *start = s;
  int neg = 0;
  int any = 0;
  long double val = 0.0L;

  *bad = 0;

  if (*s == '+' || *s == '-') {
    neg = (*s == '-');
    ++s;
  }

  if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') &&
      (s[2] == 'f' || s[2] == 'F')) {
    s += 3;
    if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') &&
        (s[2] == 'i' || s[2] == 'I') && (s[3] == 't' || s[3] == 'T') &&
        (s[4] == 'y' || s[4] == 'Y')) {
      s += 5;
    }
    any = 1;
    val = __builtin_huge_vall();
    goto done;
  }
  if ((s[0] == 'n' || s[0] == 'N') && (s[1] == 'a' || s[1] == 'A') &&
      (s[2] == 'n' || s[2] == 'N')) {
    s += 3;
    if (s[0] == '(') { /* nan(chars): skip to the closing ')' */
      const char *close = s;
      while (*close != '\0' && *close != ')') ++close;
      if (*close == ')') ++close;
      s = close;
    }
    any = 1;
    val = __builtin_nanl("");
    goto done;
  }

  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    /* hex float: mantissa*16^intpart * 2^-4*fracdigits * 2^exp */
    int frac_bits = 0;
    int exp = 0;
    s += 2;
    while (clib_hexval(*s) >= 0) {
      val = val * 16.0L + (long double)clib_hexval(*s);
      any = 1;
      ++s;
    }
    if (*s == '.') {
      ++s;
      while (clib_hexval(*s) >= 0) {
        val = val * 16.0L + (long double)clib_hexval(*s);
        frac_bits += 4;
        any = 1;
        ++s;
      }
    }
    if (!any) {
      goto done; /* "0x" with no digits: no conversion */
    }
    if (frac_bits > 0) {
      val /= clib_pow2l(frac_bits);
    }
    if (*s == 'p' || *s == 'P') {
      const char *es = s + 1;
      if (clib_read_exp(&es, &exp)) {
        s = es;
        val *= clib_pow2l(exp);
      }
    }
  } else {
    /* decimal */
    int frac_digits = 0;
    int exp = 0;
    while (*s >= '0' && *s <= '9') {
      val = val * 10.0L + (long double)(*s - '0');
      any = 1;
      ++s;
    }
    if (*s == '.') {
      ++s;
      while (*s >= '0' && *s <= '9') {
        val = val * 10.0L + (long double)(*s - '0');
        ++frac_digits;
        any = 1;
        ++s;
      }
    }
    if (!any) {
      goto done;
    }
    if (frac_digits > 0) {
      val /= clib_pow10l((unsigned int)frac_digits);
    }
    if (*s == 'e' || *s == 'E') {
      const char *es = s + 1;
      if (clib_read_exp(&es, &exp)) {
        s = es;
        if (exp != 0) {
          val *= (exp < 0) ? 1.0L / clib_pow10l((unsigned int)(-exp))
                           : clib_pow10l((unsigned int)exp);
        }
      }
    }
  }

done:
  if (!any) {
    *bad = 1;
    s = start;
    val = 0.0L;
  }
  if (neg) {
    val = -val;
  }
  if (end != 0) {
    *end = (char *)s;
  }
  return val;
}

/* ------------------------------------------------------------------ */
/* public surface                                                      */
/* ------------------------------------------------------------------ */

double strtod(const char *np, char **end) {
  int bad;
  return (double)clib_strtopd(np, end, &bad);
}

float strtof(const char *np, char **end) {
  int bad;
  return (float)clib_strtopd(np, end, &bad);
}

long double strtold(const char *np, char **end) {
  int bad;
  return clib_strtopd(np, end, &bad);
}

long double ldexpl(long double x, int exp) {
  if (x == 0.0L || exp == 0) {
    return x;
  }
  return x * ((exp < 0) ? clib_pow2l(exp) : clib_pow2l(exp));
}
