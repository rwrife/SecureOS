/**
 * @file ctx_switch.h
 * @brief Minimal setjmp/longjmp for cooperative context switching.
 *
 * Purpose:
 *   Provides save/restore of CPU registers so that a spin-wait inside a
 *   deeply nested call stack can yield back to the caller (session_manager_tick)
 *   and resume where it left off on the next tick.
 *
 * Used by:
 *   - session_manager.c: wraps console_process_injected in ctx_save
 *   - console.c: console_idle_wait calls ctx_resume to yield back
 */
#ifndef SECUREOS_CTX_SWITCH_H
#define SECUREOS_CTX_SWITCH_H

#include <stdint.h>

/**
 * Saved register context (callee-saved registers + rsp + rip).
 * Must be at least 8-byte aligned.
 */
typedef struct {
  uint64_t rbx;
  uint64_t rbp;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rsp;
  uint64_t rip;
} ctx_jmp_buf_t;

/**
 * Save the current context. Returns 0 on initial save, non-zero value
 * passed to ctx_resume on restore.
 */
int ctx_save(ctx_jmp_buf_t *buf);

/**
 * Restore a previously saved context. Does not return to the caller;
 * instead returns to the ctx_save site with the given value (must be != 0).
 */
void ctx_resume(ctx_jmp_buf_t *buf, int value) __attribute__((noreturn));

#endif /* SECUREOS_CTX_SWITCH_H */
