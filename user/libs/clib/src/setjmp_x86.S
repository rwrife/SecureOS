/*
 * user/libs/clib/src/setjmp_x86.S
 *
 * Freestanding setjmp / longjmp for the in-OS toolchain libc.
 * (issue #407 / M7-TOOLCHAIN-004 slice 7, plan
 *  plans/2026-05-28-in-os-toolchain-self-hosting.md P3, issue #446).
 *
 * Implements TWO ABIs in one preprocessed asm TU:
 *
 *   __i386__   -> i386 SysV: callee-saved set is { ebx, esi, edi, ebp,
 *                 esp, return-eip } (6 slots). This is the on-target
 *                 build per BUILD_ROADMAP §3.
 *
 *   __x86_64__ -> x86_64 SysV: callee-saved set is { rbx, rbp, r12,
 *                 r13, r14, r15, rsp, return-rip } (8 slots). Shipped
 *                 so the host harness (which runs as x86_64-linux-gnu)
 *                 can deterministically round-trip the same `cc`
 *                 invocation pattern the other clib slices use. On the
 *                 32-bit on-target build, this branch is preprocessed
 *                 out entirely.
 *
 * jmp_buf layout (matches `include/clib/setjmp.h`, MUST stay in sync):
 *
 *   i386 (4-byte slots):
 *     [0] ebx
 *     [4] esi
 *     [8] edi
 *     [12] ebp
 *     [16] esp   (caller's esp, after the retaddr is popped)
 *     [20] eip   (return address of the setjmp() call)
 *
 *   x86_64 (8-byte slots):
 *     [0]  rbx
 *     [8]  rbp
 *     [16] r12
 *     [24] r13
 *     [32] r14
 *     [40] r15
 *     [48] rsp   (caller's rsp, after the retaddr is popped)
 *     [56] rip   (return address of the setjmp() call)
 *
 * Per ISO C §7.13.2.1¶3, longjmp(env, 0) returns 1 from setjmp; any
 * other val returns val unchanged.
 *
 * No syscalls. No globals. Pure register/stack manipulation.
 */

	.text

#if defined(__i386__)

	.globl	setjmp
	.type	setjmp, @function
setjmp:
	movl	4(%esp), %eax		/* env */
	movl	%ebx,  0(%eax)
	movl	%esi,  4(%eax)
	movl	%edi,  8(%eax)
	movl	%ebp, 12(%eax)
	leal	4(%esp), %ecx		/* caller's esp (post-retaddr) */
	movl	%ecx, 16(%eax)
	movl	(%esp), %ecx		/* retaddr -> saved eip */
	movl	%ecx, 20(%eax)
	xorl	%eax, %eax
	ret
	.size	setjmp, .-setjmp

	.globl	longjmp
	.type	longjmp, @function
longjmp:
	movl	4(%esp), %edx		/* env */
	movl	8(%esp), %eax		/* val */
	testl	%eax, %eax
	jnz	1f
	movl	$1, %eax		/* ISO C: val==0 -> return 1 */
1:
	movl	 0(%edx), %ebx
	movl	 4(%edx), %esi
	movl	 8(%edx), %edi
	movl	12(%edx), %ebp
	movl	16(%edx), %esp
	jmp	*20(%edx)
	.size	longjmp, .-longjmp

#elif defined(__x86_64__)

	.globl	setjmp
	.type	setjmp, @function
setjmp:
	/* %rdi = env */
	movq	%rbx,  0(%rdi)
	movq	%rbp,  8(%rdi)
	movq	%r12, 16(%rdi)
	movq	%r13, 24(%rdi)
	movq	%r14, 32(%rdi)
	movq	%r15, 40(%rdi)
	leaq	8(%rsp), %rax		/* caller's rsp (post-retaddr) */
	movq	%rax, 48(%rdi)
	movq	(%rsp), %rax		/* retaddr -> saved rip */
	movq	%rax, 56(%rdi)
	xorl	%eax, %eax
	ret
	.size	setjmp, .-setjmp

	.globl	longjmp
	.type	longjmp, @function
longjmp:
	/* %rdi = env, %esi = val */
	movl	%esi, %eax
	testl	%eax, %eax
	jnz	1f
	movl	$1, %eax		/* ISO C: val==0 -> return 1 */
1:
	movq	 0(%rdi), %rbx
	movq	 8(%rdi), %rbp
	movq	16(%rdi), %r12
	movq	24(%rdi), %r13
	movq	32(%rdi), %r14
	movq	40(%rdi), %r15
	movq	48(%rdi), %rsp
	jmpq	*56(%rdi)
	.size	longjmp, .-longjmp

#else
# error "user/libs/clib/src/setjmp_x86.S: unsupported architecture (need __i386__ or __x86_64__)"
#endif

/* Mark the stack as non-executable. Required by toolchains that
 * default to executable stacks otherwise (no nested-function trampoline
 * lives here). */
#if defined(__linux__) || defined(__ELF__)
	.section .note.GNU-stack,"",@progbits
#endif
