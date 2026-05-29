#ifndef CLIB_STDARG_H
#define CLIB_STDARG_H

/*
 * user/libs/clib/include/clib/stdarg.h
 *
 * Freestanding `<stdarg.h>` nucleus for the in-OS toolchain libc
 * (issue #407 / M7-TOOLCHAIN-004 slice 6, plan
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3).
 *
 * TinyCC's preprocessor (`tccpp.c`), generator (`tccgen.c`), and
 * diagnostic paths (`tcc_error*`, `tcc_warning*`) take `va_list` and
 * walk it via `va_start` / `va_arg` / `va_end` / `va_copy`. The host
 * `<stdarg.h>` is one of the freestanding headers that the C standard
 * (C11 §4¶6) requires even on a non-hosted implementation, so the
 * cleanest way to ship it for the in-OS toolchain is to forward
 * directly to the `__builtin_va_*` intrinsics every supported compiler
 * (gcc, clang, tcc itself) provides.
 *
 * Why intrinsics-only (no inline asm, no struct layout)
 * -----------------------------------------------------
 *   - The x86_64 System V `va_list` is a 24-byte struct with overflow
 *     and gp_offset / fp_offset fields, walked under a contract that
 *     the *compiler* generates the right prologue for. There is no
 *     portable way to write `va_arg` by hand in C without coordinating
 *     with the calling-convention emission that gcc/clang/tcc already
 *     do for us. Forwarding to `__builtin_va_*` is the only correct
 *     freestanding implementation, and it is also what musl, newlib,
 *     and glibc's `<stdarg.h>` reduce to on modern compilers.
 *   - No syscall dependency, no allocator dependency, no globals. The
 *     macros expand to compiler intrinsics, so this header has zero
 *     runtime footprint and links cleanly against `libclib.a` without
 *     adding a source file. (We still ship a translation unit, so
 *     `symbol_set_pinned` has something concrete to test against —
 *     see notes in `src/stdarg.c`.)
 *
 * Compatibility:
 *   - gcc and clang have provided `__builtin_va_*` for over two
 *     decades; the SecureOS host toolchain (`secureos/toolchain:
 *     bookworm-2026-02-12`, clang/gcc) and TinyCC itself all
 *     implement them under those exact spellings.
 *   - `va_copy` was standardised in C99 and is also a builtin under
 *     the same name (`__builtin_va_copy`).
 *
 * Contract (matches the canonical ISO C surface):
 *
 *     typedef <impl-defined> va_list;
 *     void va_start(va_list ap, last_named_param);
 *     T    va_arg  (va_list ap, T);
 *     void va_end  (va_list ap);
 *     void va_copy (va_list dst, va_list src);
 *
 * ABI status: userland-only, additive. No `OS_ABI_VERSION` bump.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * `va_list` is whatever the compiler says it is. We deliberately do
 * NOT expose any struct layout — that is opaque to the consumer and
 * controlled entirely by the compiler's calling convention emission.
 */
typedef __builtin_va_list va_list;

/*
 * Standard surface, forwarded to the builtins. These deliberately
 * use the canonical libc names so TinyCC (#408) and SDK consumers
 * (#403) link unchanged.
 */
#define va_start(ap, last) __builtin_va_start((ap), (last))
#define va_arg(ap, type)   __builtin_va_arg((ap), type)
#define va_end(ap)         __builtin_va_end((ap))
#define va_copy(dst, src)  __builtin_va_copy((dst), (src))

#ifdef __cplusplus
}
#endif

#endif /* CLIB_STDARG_H */
