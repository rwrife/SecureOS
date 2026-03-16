/**
 * @file debug_exit.c
 * @brief QEMU debug-exit device driver for x86.
 *
 * Purpose:
 *   Provides a mechanism to terminate QEMU from within the guest by
 *   writing an exit code to the ISA debug-exit I/O port (0xF4).  This
 *   enables automated test harnesses to signal pass/fail results back
 *   to the host.
 *
 * Interactions:
 *   - cap_gate.c wraps debug_exit_qemu behind the CAP_DEBUG_EXIT
 *     capability gate so that only authorized subjects may trigger a
 *     VM exit.
 *   - console.c calls the gated exit path on the "exit" shell command.
 *   - Test harness scripts inspect the QEMU exit code to determine
 *     test outcomes.
 *
 * Launched by:
 *   Not a standalone process. Called on demand through the capability
 *   gate. Compiled into the kernel image.
 */

#include "debug_exit.h"

#define DEBUG_EXIT_PORT 0xF4

static inline void outb(unsigned short port, unsigned char val) {
  __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

void debug_exit_qemu(uint8_t code) {
  outb(DEBUG_EXIT_PORT, code);
}
