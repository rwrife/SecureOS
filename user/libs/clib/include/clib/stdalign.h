/*
 * include/clib/stdalign.h
 * Freestanding userland <stdalign.h> nucleus (M7-TOOLCHAIN-004, issue #407).
 *
 * Purpose:
 *   ISO C11 §7.15 / Annex B.10 defines `<stdalign.h>` as one of the
 *   freestanding-required headers (§4¶6). It exposes the keyword-spelled
 *   alignment operators as convenience macros plus two feature-test
 *   macros that confirm the header is present.
 *
 *   TinyCC (#408), the freestanding stdlib slice (#428), and any
 *   third-party SDK code consumed by the in-OS toolchain (#403) are
 *   entitled to `#include <stdalign.h>`. Peer freestanding headers
 *   landed in this libc subset (`<stddef.h>` PR #436, `<stdint.h>`
 *   PR #437, `<limits.h>` PR #434, `<stdarg.h>` PR #431,
 *   `<stdbool.h>` PR #435, `<iso646.h>` PR #439) all use the same
 *   "header + tiny helper TU + drift-anchor host test" shape; this
 *   slice mirrors it.
 *
 * Containment:
 *   Freestanding. No libc, no kernel includes, no syscalls. The header
 *   is pure macro definitions; the helper TU is pure integer constant
 *   expressions folded by the compiler.
 *
 * Symbol set (pinned by the `symbol_set_pinned` host-test sub-marker,
 * matches ISO C11 §7.15¶1 verbatim):
 *   alignas, alignof, __alignas_is_defined, __alignof_is_defined.
 *
 * Drift discipline:
 *   - C11 §7.1.3¶1 reserves `alignas` and `alignof` as macros; the
 *     feature-test macros MUST both expand to the integer constant `1`.
 *     The host unit test verifies each macro is `#defined` and that the
 *     LINKED helper TU folds each one to the expected value, so a
 *     future regression that drops one (or changes a feature-test
 *     macro to `0`) flips the bundle, not just the next preprocess.
 *   - No `OS_ABI_VERSION` bump: userland-only, additive header.
 *
 * References:
 *   - C11 §4¶6           (freestanding headers list)
 *   - C11 §6.7.5         (_Alignas specifier)
 *   - C11 §6.5.3.4       (_Alignof operator)
 *   - C11 §7.1.3¶1       (reserved identifiers)
 *   - C11 §7.15 / B.10   (<stdalign.h> required macros)
 */

#ifndef CLIB_STDALIGN_H
#define CLIB_STDALIGN_H

/* C11 §7.15¶1: `alignas` and `alignof` are macros that expand to the
 * corresponding keywords `_Alignas` and `_Alignof`. They are NOT
 * parenthesised: `alignas(16) int x;` must parse as a declaration
 * specifier, and `alignof(T)` must parse as a unary operator. */
#define alignas _Alignas
#define alignof _Alignof

/* C11 §7.15¶2: both feature-test macros MUST expand to the integer
 * constant `1`. Consumers (and conformance tests) use them in
 * `#if __alignas_is_defined` guards. */
#define __alignas_is_defined 1
#define __alignof_is_defined 1

#endif /* CLIB_STDALIGN_H */
