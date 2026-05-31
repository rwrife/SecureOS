/*
 * include/clib/iso646.h
 * Freestanding userland <iso646.h> nucleus (M7-TOOLCHAIN-004, issue #407).
 *
 * Purpose:
 *   ISO C11 §7.9 / Annex B.4 defines `<iso646.h>` as one of the
 *   freestanding-required headers (§4¶6). It provides 11 macros that
 *   expand to C operator tokens, intended for source written on input
 *   devices that lack the punctuation glyphs (notably ISO 646 national
 *   variants). TinyCC (#408) preprocesses translation units that may
 *   `#include <iso646.h>`; the freestanding stdlib slice (#428), the
 *   ctype slice (#417), and any third-party SDK code consumed by the
 *   in-OS toolchain (#403) are also entitled to expect it.
 *
 *   This is a header-only nucleus: there is no `.c` to link. To stay
 *   parity-shaped with the other #407 slices (which each carry a tiny
 *   src/<name>.c helper TU for `symbol_set_pinned` drift detection),
 *   we add `src/iso646.c` whose only job is to fold each macro into a
 *   constant the host test can round-trip.
 *
 * Containment:
 *   Freestanding. No libc, no kernel includes, no syscalls. Header is
 *   pure macro definitions; the helper TU is pure integer constant
 *   expressions.
 *
 * Symbol set (pinned by the symbol_set_pinned host-test sub-marker,
 * matches ISO C11 §7.9¶1 verbatim):
 *   and, and_eq, bitand, bitor, compl, not, not_eq, or, or_eq, xor,
 *   xor_eq.
 *
 * Drift discipline:
 *   - C11 §7.1.3¶1 reserves these spellings; they MUST be defined as
 *     macros and MUST NOT be redefined. The host test verifies each
 *     one is `#defined` (via `#ifdef`) and that the helper TU folds it
 *     to the expected integer value of the equivalent operator
 *     expression. A regression that drops a macro or rewrites it to a
 *     different operator flips the bundle.
 *   - No `OS_ABI_VERSION` bump: userland-only, additive header.
 *
 * References:
 *   - C11 §4¶6              (freestanding headers list)
 *   - C11 §7.1.3¶1          (reserved identifiers)
 *   - C11 §7.9 / Annex B.4  (<iso646.h> required macros)
 *   - C++23 [support.types.layout] tabulates the same set (peer impls
 *     keep the spellings stable across decades — no drift risk on the
 *     C side).
 */

#ifndef CLIB_ISO646_H
#define CLIB_ISO646_H

/* C11 §7.9¶1: each macro expands to the corresponding C operator
 * token. The parentheses are intentionally omitted: the standard
 * mandates these are bare operator tokens, not parenthesised
 * expressions (so e.g. `a and_eq b` parses as `a &= b`, not the ill-
 * formed `a (&=) b`).
 */
#define and    &&
#define and_eq &=
#define bitand &
#define bitor  |
#define compl  ~
#define not    !
#define not_eq !=
#define or     ||
#define or_eq  |=
#define xor    ^
#define xor_eq ^=

#endif /* CLIB_ISO646_H */
