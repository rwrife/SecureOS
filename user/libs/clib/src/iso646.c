/**
 * @file src/iso646.c
 * @brief Freestanding <iso646.h> drift-anchor helper TU for user/libs/clib
 *        (issue #407 slice, plan
 *        plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * `<iso646.h>` is header-only — the standard mandates 11 macros that
 * expand to operator tokens, so there is no implementation to link.
 * This TU's only job is to fold each macro into a constant integer
 * expression at THIS TU's compile time, so the host unit test can
 * round-trip the result through the LINKED `libclib` and detect any
 * future drift that redefined a macro to the wrong operator (e.g. a
 * stray `#define or &&` would silently miscompile real code; the
 * helper TU catches it at link/run time, not just at preprocess time).
 *
 * No libc dependency, no syscalls. Compiles under -ffreestanding.
 */

#include "../include/clib/iso646.h"

/* Sub-selector enum lets the test ask the helper for each macro's
 * folded value without a separate exported symbol per macro (one
 * symbol per macro would inflate the symbol set and complicate the
 * symbol_set_pinned check). Values are stable; do not reorder. */
enum clib_iso646_op {
  CLIB_ISO646_OP_AND        = 0,  /* and:    1 && 1 == 1 */
  CLIB_ISO646_OP_AND_FALSE  = 1,  /* and:    1 && 0 == 0 */
  CLIB_ISO646_OP_AND_EQ     = 2,  /* and_eq: 0x0F &= 0xF0 -> 0x00 */
  CLIB_ISO646_OP_BITAND     = 3,  /* bitand: 0xF0 & 0x0F == 0x00 */
  CLIB_ISO646_OP_BITAND_HI  = 4,  /* bitand: 0xFF & 0xF0 == 0xF0 */
  CLIB_ISO646_OP_BITOR      = 5,  /* bitor:  0xF0 | 0x0F == 0xFF */
  CLIB_ISO646_OP_COMPL      = 6,  /* compl:  ~(unsigned)0 == UINT_MAX */
  CLIB_ISO646_OP_NOT_TRUE   = 7,  /* not:    !1 == 0 */
  CLIB_ISO646_OP_NOT_FALSE  = 8,  /* not:    !0 == 1 */
  CLIB_ISO646_OP_NOT_EQ_T   = 9,  /* not_eq: 1 != 2 == 1 */
  CLIB_ISO646_OP_NOT_EQ_F   = 10, /* not_eq: 7 != 7 == 0 */
  CLIB_ISO646_OP_OR_TRUE    = 11, /* or:     0 || 1 == 1 */
  CLIB_ISO646_OP_OR_FALSE   = 12, /* or:     0 || 0 == 0 */
  CLIB_ISO646_OP_OR_EQ      = 13, /* or_eq:  0xF0 |= 0x0F -> 0xFF */
  CLIB_ISO646_OP_XOR        = 14, /* xor:    0xFF ^ 0x0F == 0xF0 */
  CLIB_ISO646_OP_XOR_EQ     = 15, /* xor_eq: 0xF0 ^= 0xFF -> 0x0F */
  CLIB_ISO646_OP__COUNT     = 16
};

/* Fold each macro at THIS TU's compile time. Returns -1 for unknown
 * selectors so the test can pin the count, too. */
unsigned long clib_iso646_eval(int op) {
  unsigned a;
  switch (op) {
    case CLIB_ISO646_OP_AND:
      return (unsigned long)(1 and 1);
    case CLIB_ISO646_OP_AND_FALSE:
      return (unsigned long)(1 and 0);
    case CLIB_ISO646_OP_AND_EQ:
      a = 0x0Fu;  a and_eq 0xF0u;  return (unsigned long)a;
    case CLIB_ISO646_OP_BITAND:
      return (unsigned long)(0xF0u bitand 0x0Fu);
    case CLIB_ISO646_OP_BITAND_HI:
      return (unsigned long)(0xFFu bitand 0xF0u);
    case CLIB_ISO646_OP_BITOR:
      return (unsigned long)(0xF0u bitor 0x0Fu);
    case CLIB_ISO646_OP_COMPL:
      /* compl on a uint32-shaped constant. The test compares against
       * the same expression compiled in the test TU. */
      return (unsigned long)(unsigned int)(compl 0u);
    case CLIB_ISO646_OP_NOT_TRUE:
      return (unsigned long)(not 1);
    case CLIB_ISO646_OP_NOT_FALSE:
      return (unsigned long)(not 0);
    case CLIB_ISO646_OP_NOT_EQ_T:
      return (unsigned long)(1 not_eq 2);
    case CLIB_ISO646_OP_NOT_EQ_F:
      return (unsigned long)(7 not_eq 7);
    case CLIB_ISO646_OP_OR_TRUE:
      return (unsigned long)(0 or 1);
    case CLIB_ISO646_OP_OR_FALSE:
      return (unsigned long)(0 or 0);
    case CLIB_ISO646_OP_OR_EQ:
      a = 0xF0u;  a or_eq 0x0Fu;  return (unsigned long)a;
    case CLIB_ISO646_OP_XOR:
      return (unsigned long)(0xFFu xor 0x0Fu);
    case CLIB_ISO646_OP_XOR_EQ:
      a = 0xF0u;  a xor_eq 0xFFu;  return (unsigned long)a;
    default:
      return (unsigned long)-1;
  }
}

int clib_iso646_op_count(void) {
  return (int)CLIB_ISO646_OP__COUNT;
}
