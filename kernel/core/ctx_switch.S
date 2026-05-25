/**
 * @file ctx_switch.S
 * @brief x86_64 setjmp/longjmp implementation for cooperative yielding.
 *
 * Purpose:
 *   Implements ctx_save (setjmp) and ctx_resume (longjmp) in assembly.
 *   Saves/restores callee-saved registers so that a deeply nested function
 *   can yield back to the caller and resume later.
 *
 * Used by:
 *   - session_manager_tick: calls ctx_save before processing injected input
 *   - console_idle_wait: calls ctx_resume to yield when WM-managed session
 *     is blocked on auth
 */
.text
.global ctx_save
.global ctx_resume

/**
 * int ctx_save(ctx_jmp_buf_t *buf)
 *   buf is in %rdi
 *   Returns 0 on initial call, non-zero on restore via ctx_resume.
 */
ctx_save:
    /* Save callee-saved registers */
    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %r12, 16(%rdi)
    movq %r13, 24(%rdi)
    movq %r14, 32(%rdi)
    movq %r15, 40(%rdi)
    /* Save stack pointer (points to return address) */
    leaq 8(%rsp), %rax
    movq %rax, 48(%rdi)
    /* Save return address */
    movq (%rsp), %rax
    movq %rax, 56(%rdi)
    /* Return 0 */
    xorl %eax, %eax
    ret

/**
 * void ctx_resume(ctx_jmp_buf_t *buf, int value)
 *   buf in %rdi, value in %esi
 *   Does not return; jumps back to ctx_save site with value.
 */
ctx_resume:
    /* Ensure value is non-zero */
    testl %esi, %esi
    jnz 1f
    movl $1, %esi
1:
    /* Restore callee-saved registers */
    movq 0(%rdi), %rbx
    movq 8(%rdi), %rbp
    movq 16(%rdi), %r12
    movq 24(%rdi), %r13
    movq 32(%rdi), %r14
    movq 40(%rdi), %r15
    /* Restore stack pointer */
    movq 48(%rdi), %rsp
    /* Return value to ctx_save caller */
    movl %esi, %eax
    /* Jump to saved return address */
    jmpq *56(%rdi)

/**
 * void ctx_call_on_stack(void *stack_top, void (*func)(void))
 *   stack_top in %rdi, func in %rsi
 *   Switches to the new stack, calls func, then restores original stack.
 */
.global ctx_call_on_stack
ctx_call_on_stack:
    /* Save original stack pointer and base pointer */
    movq %rsp, %rdx
    movq %rbp, %rcx
    /* Switch to new stack (already 16-byte aligned by caller) */
    movq %rdi, %rsp
    /* Push original rsp so we can restore it */
    pushq %rdx
    pushq %rcx
    /* Align stack for the call (16-byte aligned minus 16 for saved regs) */
    /* Call the function */
    callq *%rsi
    /* Restore original stack */
    popq %rcx
    popq %rdx
    movq %rcx, %rbp
    movq %rdx, %rsp
    ret
