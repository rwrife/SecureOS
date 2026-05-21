# Serial HAL + PC COM Driver Plan (2026-03-17)

## Goal
Create a hardware-agnostic serial HAL so core kernel code can perform serial I/O without architecture-specific dependencies, while providing a concrete driver for standard PC COM UART hardware.

## Scope
- Add serial HAL contract in kernel/hal.
- Add PC COM serial driver in kernel/drivers/serial.
- Route core serial consumers through HAL.
- Keep build scripts (ps1/sh) synchronized.

## Design
1. HAL Interface
- Define serial backend descriptor with init, non-blocking read, write-char, write-string hooks.
- Support backend identity so future GPIO backends can coexist.
- Expose ready checks and single-primary registration pattern matching storage/network HAL.

2. Standard PC COM Driver
- Implement 16550-style I/O port UART backend.
- Default to COM1 base 0x3F8.
- Configure UART for 38400 baud divisor and 8N1 framing.
- Register backend with serial HAL and initialize through a single entry point.

3. Integration
- In kmain, initialize serial via pc_com_serial_init_primary().
- In console/session manager, replace direct architecture serial calls with serial HAL calls.
- Keep legacy arch/x86/serial API as compatibility facade to reduce migration friction.

## Validation
- Build kernel entry/link flow using build/scripts/build_kernel_entry.ps1.
- Verify the shell script mirror (build/scripts/build_kernel_entry.sh) has equivalent compile/link units.
- Manual QEMU smoke test should still emit serial boot markers and shell prompt.

## Follow-up
- Add GPIO/bit-banged UART backend for non-PC hardware and select backend by architecture/board profile.
- Add a serial HAL unit test harness with a mock backend for readiness and dispatch behavior.
