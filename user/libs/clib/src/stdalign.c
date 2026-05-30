/**
 * @file src/stdalign.c
 * @brief Freestanding <stdalign.h> drift-anchor helper TU for
 *        user/libs/clib (issue #407 slice, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * `<stdalign.h>` is header-only — the standard mandates four macros
 * (two operator-keyword aliases plus two feature-test constants), so
 * there is no implementation to link. This TU's only job is to fold
 * each macro into a constant integer expression at THIS TU's compile
 * time so the host unit test can round-trip the result through the
 * LINKED `libclib` and detect any future drift (e.g. a stray
 * `#define __alignof_is_defined 0`, or a redefinition of `alignas` to
 * something other than `_Alignas`).
 *
 * No libc dependency, no syscalls. Compiles under `-ffreestanding`.
 */

#include "../include/clib/stdalign.h"

/* Sub-selector enum lets the test ask the helper for each macro's
 * folded value without exporting one symbol per macro (which would
 * inflate the symbol set and complicate the `symbol_set_pinned`
 * check). Values are stable; do not reorder.
 *
 * Coverage:
 *   - The two feature-test macros are checked directly for the literal
 *     `1` mandated by C11 §7.15¶2.
 *   - `alignas` and `alignof` are checked semantically: `alignof(T)`
 *     for a few well-known scalar types must equal the same value
 *     `_Alignof(T)` produces (we fold both at this TU's compile time
 *     so the comparison can run on any host), and a struct field
 *     forced with `alignas(N)` must observe alignment `N`. The test
 *     re-derives the expected values on its own side and compares,
 *     so the round-trip pins the LINKED-libclib view rather than just
 *     what the test TU's include happened to see. */
enum clib_stdalign_op {
  CLIB_STDALIGN_OP_ALIGNAS_IS_DEFINED = 0,
  CLIB_STDALIGN_OP_ALIGNOF_IS_DEFINED = 1,
  CLIB_STDALIGN_OP_ALIGNOF_CHAR       = 2,
  CLIB_STDALIGN_OP_ALIGNOF_INT        = 3,
  CLIB_STDALIGN_OP_ALIGNOF_DOUBLE     = 4,
  CLIB_STDALIGN_OP_ALIGNOF_LONG       = 5,
  CLIB_STDALIGN_OP_ALIGNAS_FIELD_16   = 6, /* alignof an alignas(16) field */
  CLIB_STDALIGN_OP_ALIGNAS_FIELD_32   = 7, /* alignof an alignas(32) field */
  CLIB_STDALIGN_OP__COUNT             = 8
};

/* Force the `alignas` macro to participate in real declarations so a
 * regression that mis-defines it as anything other than `_Alignas`
 * fails to compile this TU (belt + suspenders: the host test also
 * pins the alignment values at run time). */
struct clib_stdalign_field16 {
  char pad;
  alignas(16) char a;
};

struct clib_stdalign_field32 {
  char pad;
  alignas(32) char a;
};

/* Fold each macro at THIS TU's compile time. Returns (unsigned long)-1
 * for unknown selectors so the test can pin the count, too. */
unsigned long clib_stdalign_eval(int op) {
  switch (op) {
    case CLIB_STDALIGN_OP_ALIGNAS_IS_DEFINED:
      return (unsigned long)__alignas_is_defined;
    case CLIB_STDALIGN_OP_ALIGNOF_IS_DEFINED:
      return (unsigned long)__alignof_is_defined;
    case CLIB_STDALIGN_OP_ALIGNOF_CHAR:
      return (unsigned long)alignof(char);
    case CLIB_STDALIGN_OP_ALIGNOF_INT:
      return (unsigned long)alignof(int);
    case CLIB_STDALIGN_OP_ALIGNOF_DOUBLE:
      return (unsigned long)alignof(double);
    case CLIB_STDALIGN_OP_ALIGNOF_LONG:
      return (unsigned long)alignof(long);
    case CLIB_STDALIGN_OP_ALIGNAS_FIELD_16:
      return (unsigned long)alignof(struct clib_stdalign_field16);
    case CLIB_STDALIGN_OP_ALIGNAS_FIELD_32:
      return (unsigned long)alignof(struct clib_stdalign_field32);
    default:
      return (unsigned long)-1;
  }
}

int clib_stdalign_op_count(void) {
  return (int)CLIB_STDALIGN_OP__COUNT;
}
