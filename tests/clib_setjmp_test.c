/*
 * tests/clib_setjmp_test.c
 *
 * Host unit test for the freestanding <setjmp.h> nucleus shipped by
 * user/libs/clib (issue #407 / M7-TOOLCHAIN-004 slice 7, plan
 * plans/2026-05-28-in-os-toolchain-self-hosting.md P3, issue #446).
 *
 * The implementation under test is hand-rolled assembly
 * (`src/setjmp_x86.S`) that snapshots/restores the i386 and x86_64
 * SysV callee-saved set. This harness links the .S file together
 * with this .c file (see `build/scripts/test_clib_setjmp.sh`) so the
 * round-trip exercises OUR setjmp/longjmp pair rather than host libc's.
 *
 * Sub-markers (each must round-trip via TEST:PASS:clib_setjmp:...):
 *   - roundtrip_nonzero        : setjmp returns 0 first; longjmp(env, 42)
 *                                causes the setjmp site to return 42.
 *   - zero_coerced_to_one      : longjmp(env, 0) -> setjmp returns 1
 *                                (ISO C §7.13.2.1¶3).
 *   - callee_saved_restored    : a callee-saved register held across
 *                                a setjmp/longjmp is observably the
 *                                value it had at setjmp time, NOT the
 *                                value the longjmp-time scratch holds.
 *   - nested_env_reuse         : after a longjmp, the same jmp_buf can
 *                                be reused for a fresh setjmp/longjmp
 *                                with a different value.
 *   - symbol_set_pinned        : setjmp + longjmp reachable through a
 *                                function pointer — drift guard.
 *
 * Roll-up marker:
 *   - TEST:PASS:clib_setjmp    (only emitted if every sub-marker
 *                               passed and zero TEST:FAIL: lines were
 *                               recorded).
 */

#include <stdio.h>

#include "../user/libs/clib/include/clib/setjmp.h"

static int g_fail = 0;

#define CHECK(cond, name)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "TEST:FAIL:clib_setjmp:%s\n", (name));                   \
      g_fail = 1;                                                              \
    }                                                                          \
  } while (0)

/* ----- roundtrip_nonzero ------------------------------------------------- */

static void test_roundtrip_nonzero(void) {
  jmp_buf env;
  volatile int passes = 0;

  int r = setjmp(env);
  if (r == 0) {
    passes = 1;
    longjmp(env, 42);
    /* unreachable */
  } else {
    int ok = (r == 42) && (passes == 1);
    CHECK(ok, "roundtrip_nonzero");
    if (ok) printf("TEST:PASS:clib_setjmp:roundtrip_nonzero\n");
  }
}

/* ----- zero_coerced_to_one ---------------------------------------------- */

static void test_zero_coerced_to_one(void) {
  jmp_buf env;

  int r = setjmp(env);
  if (r == 0) {
    longjmp(env, 0);
    /* unreachable */
  } else {
    int ok = (r == 1); /* ISO C: val==0 must become 1 */
    CHECK(ok, "zero_coerced_to_one");
    if (ok) printf("TEST:PASS:clib_setjmp:zero_coerced_to_one\n");
  }
}

/* ----- callee_saved_restored -------------------------------------------- */

/*
 * Demonstrate that a callee-saved register held live across the
 * setjmp/longjmp pair is restored to its setjmp-time value, not its
 * longjmp-time value.
 *
 * We pin a local into a callee-saved register via a register-class
 * inline asm constraint ("r" + the fact that GCC/Clang will park a
 * long-lived non-address-taken local into ebx/esi/edi/ebp on i386 or
 * rbx/r12-r15 on x86_64). To make this airtight without relying on
 * the compiler's allocator, we use an explicit `register ... asm(...)`
 * binding for a known callee-saved register on each ABI.
 *
 * The local is set to a SENTINEL before setjmp, then a different
 * value AFTER setjmp returns 0, then longjmp fires. When setjmp
 * returns the second time, the local must read back as the SENTINEL.
 */
#if defined(__i386__)
#  define CALLEE_SAVED_REG_BIND  "ebx"
#elif defined(__x86_64__)
#  define CALLEE_SAVED_REG_BIND  "r12"
#else
#  error "test only supports i386 / x86_64"
#endif

static void test_callee_saved_restored(void) {
  jmp_buf env;
  static volatile int turn = 0;
  register unsigned long pinned __asm__(CALLEE_SAVED_REG_BIND);

  pinned = 0xC0FFEEUL;          /* SENTINEL */
  __asm__ volatile("" : "+r"(pinned)); /* tell compiler the value is live */

  int r = setjmp(env);

  /* Force the compiler to spill+reload `pinned` around the branch
   * so the post-longjmp read sees the register-restored value. */
  __asm__ volatile("" : "+r"(pinned));

  if (r == 0 && turn == 0) {
    turn = 1;
    /* Clobber the register with a different value so a broken
     * implementation that leaves the live register alone is caught. */
    pinned = 0xDEADBEEFUL;
    __asm__ volatile("" : "+r"(pinned));
    longjmp(env, 7);
    /* unreachable */
  } else {
    /* On the unwind, the pinned register MUST read back as the
     * setjmp-time SENTINEL value, not the longjmp-time clobber. */
    unsigned long observed = pinned;
    __asm__ volatile("" : "+r"(observed));

    int ok = (r == 7) && (observed == 0xC0FFEEUL);
    CHECK(ok, "callee_saved_restored");
    if (ok) printf("TEST:PASS:clib_setjmp:callee_saved_restored\n");
  }
}

/* ----- nested_env_reuse ------------------------------------------------- */

static void test_nested_env_reuse(void) {
  jmp_buf env;
  static volatile int phase = 0;

  int r = setjmp(env);
  if (phase == 0 && r == 0) {
    phase = 1;
    longjmp(env, 11);
  } else if (phase == 1 && r == 11) {
    /* Reuse the same buf for a fresh round-trip. */
    phase = 2;
    int r2 = setjmp(env);
    if (r2 == 0) {
      longjmp(env, 22);
    } else {
      int ok = (r2 == 22);
      CHECK(ok, "nested_env_reuse");
      if (ok) printf("TEST:PASS:clib_setjmp:nested_env_reuse\n");
    }
  } else {
    CHECK(0, "nested_env_reuse");
  }
}

/* ----- symbol_set_pinned ------------------------------------------------ */

/*
 * Drift guard, parity with the str/mem (#416), ctype (#417),
 * qsort (#418), stdlib (#428), errno (#430), and stdarg (#431) slices.
 * If a future refactor accidentally drops setjmp or longjmp from the
 * public surface, the pointer array contains a NULL and the assert
 * fails — which would also fail to link earlier, but the explicit
 * audit anchor matches the pattern used by every other clib slice.
 */
static void test_symbol_set_pinned(void) {
  void *const symbols[] = {
      (void *)&setjmp,
      (void *)&longjmp,
  };
  const int n = (int)(sizeof(symbols) / sizeof(symbols[0]));
  int ok = 1;
  for (int i = 0; i < n; ++i) {
    if (symbols[i] == 0) { ok = 0; break; }
  }
  /* jmp_buf typedef must still resolve. */
  if (sizeof(jmp_buf) < sizeof(unsigned long) * 6) ok = 0;
  CHECK(ok, "symbol_set_pinned");
  if (ok) printf("TEST:PASS:clib_setjmp:symbol_set_pinned\n");
}

int main(void) {
  test_roundtrip_nonzero();
  test_zero_coerced_to_one();
  test_callee_saved_restored();
  test_nested_env_reuse();
  test_symbol_set_pinned();

  if (!g_fail) {
    printf("TEST:PASS:clib_setjmp\n");
    return 0;
  }
  fprintf(stderr, "TEST:FAIL:clib_setjmp\n");
  return 1;
}
