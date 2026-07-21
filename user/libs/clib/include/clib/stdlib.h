/*
 * include/clib/stdlib.h
 * Freestanding userland numeric-conversion + integer-utility family
 * (M7-TOOLCHAIN-004 slice 4, issue #407).
 *
 * Purpose:
 *   Slice 4 of the in-OS toolchain freestanding libc (`user/libs/clib`,
 *   plan `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3). TinyCC's
 *   driver (tcc.c) and linker (tccelf.c) call `atoi` / `strtol` /
 *   `strtoul` / `abs` / `labs` to parse `-On`, `-Wl,--section-start=ADDR`,
 *   numeric `-D` macro values, and to clamp signed offsets. None of
 *   these need syscalls or locale support, so they can land before stdio
 *   and setjmp — same containment posture as slices 1/2/3 (str/mem,
 *   ctype, qsort).
 *
 * Containment:
 *   Freestanding. No libc, no kernel includes, no syscalls. Pure
 *   per-character arithmetic on the ASCII subset. Out-of-range parses
 *   clamp to LONG_{MIN,MAX} / ULONG_MAX and set `*endptr` past the
 *   consumed digits — matches the C99 contract that TinyCC depends on.
 *
 * Symbol set (pinned by the symbol_set_pinned host-test sub-marker):
 *   Numeric parse:
 *     - atoi
 *     - strtol
 *     - strtoul
 *     - strtoll  (long long path; added by the slice 11 extension)
 *     - strtoull (unsigned long long path; added by the slice 11 extension)
 *   Integer utilities:
 *     - abs
 *     - labs
 *   Program-status constants (slice 4b, added 2026-05-31):
 *     - EXIT_SUCCESS  (C11 §7.22, value 0)
 *     - EXIT_FAILURE  (C11 §7.22, value 1 — implementation-defined
 *                      non-zero; we pin 1 to match TinyCC's expectation
 *                      and the glibc/musl/newlib convention)
 *
 * Out of scope for this slice (folded in by later #407 slices):
 *   - atof / strtod (floating point; TinyCC reads no float command-line
 *     args today).
 *   - rand / srand (PRNG; no consumer yet).
 *   - getenv / system (require runtime wiring beyond this slice).
 *   - bsearch (no consumer yet; qsort already shipped in slice 3).
 *
 * Naming:
 *   Symbols use the canonical libc names (`atoi`, `strtol`, ...) so
 *   TinyCC links unchanged. The host unit test compiles this file with
 *   `-fno-builtin` so the assertions exercise OUR implementations
 *   rather than `__builtin_*`.
 *
 * Interactions:
 *   - src/stdlib.c              — implementation (table-free, branch-thin).
 *   - tests/clib_stdlib_test.c  — host unit test, compiled with
 *                                 -fno-builtin.
 *   - build/scripts/test_clib_stdlib.sh — TEST:PASS:clib_stdlib driver.
 *
 * ABI status:
 *   Userland-only, additive. No `OS_ABI_VERSION` bump (parity with
 *   slices 1/2/3 and `clib_malloc`).
 *
 * Issue: 407. Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md (P3).
 */

#ifndef SECUREOS_USER_LIBS_CLIB_STDLIB_H
#define SECUREOS_USER_LIBS_CLIB_STDLIB_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- program-status constants ------------------------------------------
 *
 * C11 §7.22 mandates `EXIT_SUCCESS` and `EXIT_FAILURE` as macros that
 * expand to integer constant expressions usable as the argument to
 * `exit()` (and, by extension, as a `main()` return value). The
 * standard fixes `EXIT_SUCCESS` at the same status as a `0` return;
 * `EXIT_FAILURE` is implementation-defined non-zero. We pin both
 * values explicitly so:
 *
 *   - TinyCC's driver (`tcc.c`, #408) — which spells exit status as
 *     `return EXIT_FAILURE;` on a failed parse / link — links
 *     unchanged against `libclib.a`.
 *   - The on-target `cc` driver app (#409) propagates a numerically
 *     stable failure status to the sosh `$?` round-trip pinned by
 *     #406's `os_process_exit` wiring.
 *   - The host unit test (`clib_stdlib:exit_macros`) can pin the exact
 *     values rather than "any non-zero int" — keeps regressions
 *     detectable.
 *
 * Value choices match the glibc / musl / newlib / TinyCC convention
 * (0 / 1). `exit()` is declared below and implemented as an
 * `os_process_exit` forwarder in `src/stdlib.c`.
 */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void exit(int status) __attribute__((noreturn));

/* --- numeric parse ------------------------------------------------------ */

/*
 * atoi(s)
 *   Skip leading isspace() characters, accept an optional sign, then
 *   consume decimal digits until the first non-digit. Return value is
 *   undefined on overflow (matches the C standard); this implementation
 *   wraps via the same path strtol uses but does NOT clamp — callers
 *   that need clamping must use strtol.
 */
int atoi(const char *s);

/*
 * strtol(nptr, endptr, base)
 *   - Skip leading isspace().
 *   - Accept optional '+' / '-'.
 *   - If base==0:  "0x"/"0X" -> base 16; leading "0" -> base 8; else 10.
 *   - If base==16: optional "0x"/"0X" prefix is consumed.
 *   - Consume digits valid for the resolved base.
 *   - On overflow: return LONG_MAX / LONG_MIN and (per POSIX) set errno
 *     to ERANGE — this freestanding build has no errno, so the overflow
 *     bit is exposed by the value alone.
 *   - *endptr (if non-NULL) is set to the first unconverted character;
 *     if no digits were consumed, *endptr == nptr.
 */
long strtol(const char *nptr, char **endptr, int base);

/*
 * strtoul(nptr, endptr, base)
 *   Same parse rules as strtol, but unsigned. A leading '-' is parsed
 *   and the result is negated modulo ULONG_MAX+1, matching the C
 *   standard contract that TinyCC's driver relies on for `-Wl,--defsym`
 *   negative offsets.
 */
unsigned long strtoul(const char *nptr, char **endptr, int base);

/*
 * strtoll(nptr, endptr, base)
 *   Same parse / overflow / *endptr rules as strtol, but accumulates
 *   into a `long long` and clamps to LLONG_MAX / LLONG_MIN. TinyCC's
 *   driver consumes this for `-D` macro values that exceed 32 bits
 *   on the x86_64 target (e.g. `-DFOO=0x80000000ULL`).
 */
long long strtoll(const char *nptr, char **endptr, int base);

/*
 * strtoull(nptr, endptr, base)
 *   Same parse rules as strtoul, but unsigned long long width. Clamps
 *   to ULLONG_MAX on overflow; negation is taken modulo ULLONG_MAX+1.
 */
unsigned long long strtoull(const char *nptr, char **endptr, int base);

/* --- integer utilities -------------------------------------------------- */

/*
 * abs(x) / labs(x)
 *   Behavior on INT_MIN / LONG_MIN is undefined per the C standard.
 *   This implementation returns the input unchanged in that case
 *   (consistent with two's-complement hardware) rather than invoking UB
 *   through `-x`.
 */
int  abs (int  x);
long labs(long x);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_STDLIB_H */
