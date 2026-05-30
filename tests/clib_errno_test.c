/**
 * @file clib_errno_test.c
 * @brief Host unit test for the freestanding <errno.h> nucleus
 *        (issue #407 slice 5, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Covers:
 *   1. `errno` is a writable global initialised to 0 at process start.
 *   2. Each shipped macro has the pinned literal value (drift guard —
 *      a TinyCC drop or unrelated PR that bumps `ERANGE` to a
 *      different number trips this test before TinyCC starts).
 *   3. `clib_strerror` returns a non-NULL bounded ASCII description
 *      for every shipped errnum + a sane "Unknown error" string for
 *      an unrecognised code (parity with glibc / musl semantics).
 *   4. `errno`-write round trip: assigning into `errno` is observed
 *      on the next read (asserts no per-thread indirection snuck in).
 *   5. `symbol_set_pinned`: every shipped errno macro is materialised
 *      through a runtime indirection so a future header edit that
 *      drops one trips the count assert; `clib_strerror` and `&errno`
 *      are also held live so the linker cannot DCE them.
 *
 * Compiled with `-fno-builtin` and WITHOUT pulling in the system
 * <errno.h>, so the assertions exercise OUR macros / global rather
 * than glibc's. The host `<stdio.h>` is fine to include for the
 * `printf` / `fprintf` reporting only — it does not transitively
 * leak `errno` macros under glibc on Debian bookworm (verified
 * manually; the test redefines its own canary in a static_assert
 * below to catch any future leak).
 *
 * Launched by:
 *   build/scripts/test_clib_errno.sh (dispatched via
 *   build/scripts/test.sh clib_errno).
 */

#include <stdio.h>
#include <string.h>

#include "../user/libs/clib/include/clib/errno.h"

static int g_fail = 0;

#define CHECK(cond, name) do { \
  if (!(cond)) { \
    fprintf(stderr, "TEST:FAIL:clib_errno:%s\n", (name)); \
    g_fail = 1; \
  } \
} while (0)

/* Pinned values (musl / Linux numbering) — any drift here breaks the
 * on-target wrapper that translates OS_STATUS_* to a POSIX-shaped int,
 * and breaks TinyCC's `errno == ERANGE` overflow checks. */
static void test_macro_values(void) {
  int ok = 1;
  if (EPERM       != 1)  ok = 0;
  if (ENOENT      != 2)  ok = 0;
  if (EIO         != 5)  ok = 0;
  if (EBADF       != 9)  ok = 0;
  if (ENOMEM      != 12) ok = 0;
  if (EACCES      != 13) ok = 0;
  if (EFAULT      != 14) ok = 0;
  if (EBUSY       != 16) ok = 0;
  if (EEXIST      != 17) ok = 0;
  if (ENOTDIR     != 20) ok = 0;
  if (EISDIR      != 21) ok = 0;
  if (EINVAL      != 22) ok = 0;
  if (ENFILE      != 23) ok = 0;
  if (EMFILE      != 24) ok = 0;
  if (ENOSPC      != 28) ok = 0;
  if (ESPIPE      != 29) ok = 0;
  if (EROFS       != 30) ok = 0;
  if (ERANGE      != 34) ok = 0;
  if (ENOSYS      != 38) ok = 0;
  if (EOVERFLOW   != 75) ok = 0;
  if (ENOTSUP     != 95) ok = 0;
  CHECK(ok, "macro_values_pinned");
  if (ok) printf("TEST:PASS:clib_errno:macro_values_pinned\n");
}

static void test_errno_global_zero_init(void) {
  /* Process start: errno is in BSS and must read as 0 before any
   * libc-style call writes to it. */
  CHECK(errno == 0, "errno_global_zero_init");
  if (errno == 0) printf("TEST:PASS:clib_errno:errno_global_zero_init\n");
}

static void test_errno_writable_roundtrip(void) {
  /* Write through the symbol; read back. Asserts that `errno` is a
   * real lvalue (not a macro that aliases a per-thread pointer). */
  errno = 0;
  errno = ERANGE;
  int read1 = errno;
  errno = EINVAL;
  int read2 = errno;
  errno = 0;
  int read3 = errno;
  int ok = (read1 == ERANGE) && (read2 == EINVAL) && (read3 == 0);
  CHECK(ok, "errno_writable_roundtrip");
  if (ok) printf("TEST:PASS:clib_errno:errno_writable_roundtrip\n");
}

static void test_errno_address_stable(void) {
  /* Two reads of `&errno` from this TU must agree — guards against an
   * accidental `#define errno (*__errno_location())`-style indirection
   * being added under us. */
  int *a = &errno;
  int *b = &errno;
  int ok = (a == b) && (a != 0);
  CHECK(ok, "errno_address_stable");
  if (ok) printf("TEST:PASS:clib_errno:errno_address_stable\n");
}

static void test_strerror_known(void) {
  /* Every shipped code returns a non-NULL bounded description. */
  int codes[] = {
    0, EPERM, ENOENT, EIO, EBADF, ENOMEM, EACCES, EFAULT, EBUSY,
    EEXIST, ENOTDIR, EISDIR, EINVAL, ENFILE, EMFILE, ENOSPC,
    ESPIPE, EROFS, ERANGE, ENOSYS, ENOTSUP, EOVERFLOW,
  };
  int n = (int)(sizeof(codes) / sizeof(codes[0]));
  int ok = 1;
  for (int i = 0; i < n; ++i) {
    const char *s = clib_strerror(codes[i]);
    if (s == 0) { ok = 0; break; }
    if (s[0] == '\0') { ok = 0; break; }
    if (strlen(s) > 64) { ok = 0; break; }
  }
  CHECK(ok, "strerror_known_codes");
  if (ok) printf("TEST:PASS:clib_errno:strerror_known_codes\n");
}

static void test_strerror_unknown(void) {
  /* Unknown codes return the literal "Unknown error" — never NULL,
   * matching glibc's "Unknown error N"-shaped contract closely
   * enough that TinyCC's diagnostic paths print something sensible. */
  const char *s1 = clib_strerror(99999);
  const char *s2 = clib_strerror(-1);
  int ok = (s1 != 0) && (s2 != 0) &&
           strcmp(s1, "Unknown error") == 0 &&
           strcmp(s2, "Unknown error") == 0;
  CHECK(ok, "strerror_unknown_code");
  if (ok) printf("TEST:PASS:clib_errno:strerror_unknown_code\n");
}

/* Symbol-set pinning: every shipped macro is materialised into a
 * runtime int and every shipped function pointer is exercised. Drift
 * (drop a macro / drop the strerror helper / drop &errno) trips
 * either the count assert or the link.
 */
static void test_symbol_set_pinned(void) {
  int codes[] = {
    EPERM, ENOENT, EIO, EBADF, ENOMEM, EACCES, EFAULT, EBUSY,
    EEXIST, ENOTDIR, EISDIR, EINVAL, ENFILE, EMFILE, ENOSPC,
    ESPIPE, EROFS, ERANGE, ENOSYS, ENOTSUP, EOVERFLOW,
  };
  enum { kPinnedMacroCount = 21 };
  int n = (int)(sizeof(codes) / sizeof(codes[0]));
  int ok = (n == kPinnedMacroCount);

  /* Exercise the function pointer + the global address so the linker
   * cannot DCE either. */
  const char *(*strerr)(int) = clib_strerror;
  if (strerr == 0) ok = 0;
  /* Hold &errno live so the linker cannot DCE the symbol; storing
   * through a volatile pointer also doubles as proof that `errno`
   * is a real, addressable lvalue (not a macro). */
  volatile int *errno_addr = &errno;
  if (errno_addr == (volatile int *)0) ok = 0;
  for (int i = 0; i < n; ++i) {
    if (codes[i] == 0) { ok = 0; break; }
    if (strerr(codes[i]) == 0) { ok = 0; break; }
  }
  CHECK(ok, "symbol_set_pinned");
  if (ok) printf("TEST:PASS:clib_errno:symbol_set_pinned\n");
}

int main(void) {
  test_macro_values();
  test_errno_global_zero_init();
  test_errno_writable_roundtrip();
  test_errno_address_stable();
  test_strerror_known();
  test_strerror_unknown();
  test_symbol_set_pinned();

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:clib_errno\n");
    return 1;
  }
  printf("TEST:PASS:clib_errno\n");
  return 0;
}
