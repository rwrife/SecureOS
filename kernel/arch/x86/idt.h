/**
 * @file idt.h
 * @brief x86-64 Interrupt Descriptor Table (IDT) setup and exception handling.
 *
 * Purpose:
 *   Provides CPU exception handling so that faults in native apps (page faults,
 *   GP faults, etc.) are caught and reported instead of crashing the kernel.
 *   Sets up the 256-entry IDT with handlers for all 32 CPU exceptions.
 *
 * Interactions:
 *   - kmain.c: calls idt_init() during boot to install exception handlers.
 *   - launcher_exec.c: uses fault_recover_set/fault_recover_active to protect
 *     native app execution with a recovery point.
 *   - entry.asm: IDT stubs defined in idt_stubs.asm push exception info and
 *     call the C handler.
 *
 * Launched by:
 *   idt_init() called from kmain() after basic hardware init.
 */

#ifndef SECUREOS_IDT_H
#define SECUREOS_IDT_H

#include <stdint.h>

/** Initialize the IDT and load it via LIDT. Must be called once at boot. */
void idt_init(void);

/**
 * Fault recovery interface — allows the kernel to survive native app crashes.
 *
 * Before calling a native app entry point, call fault_recover_set() to arm
 * the recovery mechanism. If a CPU exception occurs while recovery is armed,
 * the handler restores the saved state and returns control to the caller
 * with a non-zero return value indicating the exception vector.
 *
 * Returns 0 on first call (save point), non-zero (exception vector + 1)
 * when recovered from a fault.
 */
int fault_recover_set(void);

/** Disarm the fault recovery mechanism (call after app returns normally). */
void fault_recover_clear(void);

/** Returns 1 if fault recovery is currently armed. */
int fault_recover_active(void);

/** Get the last exception vector that triggered recovery (0-31). */
int fault_recover_last_vector(void);

/** Get the faulting instruction pointer from the last recovery. */
uint64_t fault_recover_last_rip(void);

#endif /* SECUREOS_IDT_H */
