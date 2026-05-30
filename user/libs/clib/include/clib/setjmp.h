/*
 * user/libs/clib/include/clib/setjmp.h
 *
 * Freestanding `<setjmp.h>` nucleus for the in-OS toolchain libc
 * (issue #407 / M7-TOOLCHAIN-004 slice 7, plan
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3, issue #446).
 *
 * Purpose
 *   TinyCC's error-recovery path (`tcc_error*` / `tcc_warning*`,
 *   `tccpp.c`, `tcc.c`) leans on a real `setjmp`/`longjmp` pair so a
 *   parse-time fault unwinds the compiler driver back to the top of
 *   the input file rather than bailing out of the process. This slice
 *   is therefore a gating dependency for #408 (TinyCC freestanding
 *   port).
 *
 * Why hand-rolled assembly (not `__builtin_setjmp` / `__builtin_longjmp`)
 *   The compiler builtins exist (and we considered them, in the
 *   spirit of the `<stdarg.h>` slice's "forward to intrinsics"
 *   rationale), but #446's scope explicitly calls for "the real x86
 *   freestanding pair — save/restore the i386 SysV callee-saved set
 *   (ebx, esi, edi, ebp, esp, return-eip)". A hand-rolled save/restore
 *   makes the register set we promise to preserve auditable in one
 *   file, and is what TinyCC's own host-side `tcc.c` expects.
 *
 *   The accompanying translation unit is `user/libs/clib/src/setjmp_x86.S`
 *   (preprocessed `.S`), which carries the i386 SysV implementation
 *   required by the issue body plus a parallel x86_64 SysV
 *   implementation so the host CI (which runs as `x86_64-linux-gnu`)
 *   can exercise the round-trip via the same `cc` invocation pattern
 *   the other slices use. 64-bit register save is "out of scope" for
 *   the on-target build (which is i386 per BUILD_ROADMAP §3) but
 *   shipping the x86_64 variant has no runtime cost on the target
 *   image (the linker drops it) and unblocks the deterministic host
 *   harness.
 *
 * jmp_buf storage layout
 *   We define `jmp_buf` as a fixed-size array of `unsigned long`
 *   large enough to hold either ABI's saved register set:
 *
 *     - i386  SysV: ebx, esi, edi, ebp, esp, eip   -> 6 slots (24B)
 *     - x86_64 SysV: rbx, rbp, r12, r13, r14, r15,
 *                     rsp, rip                     -> 8 slots (64B)
 *
 *   `CLIB_JMP_BUF_SLOTS = 8` covers both. The wire layout (offsets
 *   into the buffer) is the source of truth in `setjmp_x86.S` — DO
 *   NOT reorder without updating both files together.
 *
 * ABI status
 *   Userland-only, additive symbol surface. No `OS_ABI_VERSION` bump.
 *   No syscalls, no allocator, no globals.
 *
 * Out of scope
 *   `sigsetjmp` / `siglongjmp` — SecureOS userland has no signals.
 *   Floating-point / SSE state preservation — i386 SysV does not
 *   require it across function calls (the FP control word is the only
 *   bit a sane implementation would save, and TinyCC does not touch
 *   FP state in its recovery path).
 */

#ifndef SECUREOS_USER_LIBS_CLIB_SETJMP_H
#define SECUREOS_USER_LIBS_CLIB_SETJMP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Number of `unsigned long` slots in `jmp_buf`. Sized to the larger
 * of the two supported SysV ABIs (x86_64 needs 8). DO NOT shrink
 * without dropping the x86_64 implementation in `setjmp_x86.S`. */
#define CLIB_JMP_BUF_SLOTS 8

/* The classic C-typedef-of-array spelling: `jmp_buf` decays to
 * `unsigned long *` when passed as an argument, exactly like glibc /
 * musl / newlib. This is the spelling TinyCC and ISO C consumers
 * expect. */
typedef unsigned long jmp_buf[CLIB_JMP_BUF_SLOTS];

/* setjmp: snapshot callee-saved register set + stack-pointer +
 * return-address into `env`. Returns 0 on the initial call and
 * the value passed to `longjmp(env, val)` on the unwind. */
int  setjmp(jmp_buf env);

/* longjmp: unwind to the `setjmp(env)` call site, causing that
 * call to return `val`. Per ISO C §7.13.2.1¶3, `val == 0` is
 * coerced to `1` so the caller can always distinguish the
 * initial vs. unwind return.
 *
 * `__attribute__((noreturn))` is critical: the inline asm jumps
 * to the saved `eip`/`rip` rather than emitting a `ret`, so the
 * compiler must not assume control flow can fall through. */
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_SETJMP_H */
