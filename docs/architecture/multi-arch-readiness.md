# Multi-architecture readiness audit (x86-first baseline)

- Owner: kernel / architecture
- Status: active audit baseline (`OS_ABI_VERSION = 0`)
- Tracking issue: [#623](https://github.com/rwrife/SecureOS/issues/623)
- Last reviewed: 2026-07-20

This document makes `BUILD_ROADMAP.md` §2.1 operational: SecureOS is x86/QEMU
first, while preserving a disciplined path to additional architectures via HAL
and drift gates.

## 1) Arch-dependent decisions currently frozen

1. **Boot protocol and CPU mode (x86):** Multiboot v1 + in-kernel transition
   to 64-bit long mode is the accepted baseline.
   - Source: [ADR 0001](decisions/0001-boot-protocol-multiboot-long-mode.md)
   - Companion: [`docs/BOOT_ENTRY_X86.md`](../BOOT_ENTRY_X86.md)

2. **Endian and wire-format assumptions in ABI v0:** IPC and capability-handle
   layouts are little-endian under `OS_ABI_VERSION = 0`.
   - Sources:
     - [`docs/abi/ipc-wire.md`](../abi/ipc-wire.md)
     - [`docs/abi/capability-handle.md`](../abi/capability-handle.md)

3. **HAL-first hardware contract:** hardware access is expected to flow through
   `kernel/hal/*_hal.{h,c}` interfaces.
   - Source: [`kernel-module-boundaries.md`](kernel-module-boundaries.md)
   - Surface list: serial, storage, video, network, clock, input, mouse

4. **Syscall entry surface is reserved but not ring-3 finalized yet:** v0 keeps
   a stable syscall vector reservation while remaining x86-focused in current
   execution substrate.
   - Source: [`docs/abi/syscalls.md`](../abi/syscalls.md)

## 2) Snapshot: x86 assumptions outside `kernel/arch/`

Audit command snapshot used for this run:

```bash
grep -RInE '__x86_64__|__i386__|__amd64__|__aarch64__|__arm__' kernel/core kernel/mem kernel/sched kernel/ipc kernel/cap kernel/proc kernel/hal kernel/fs kernel/drivers kernel/format kernel/svc kernel/event kernel/gfx kernel/clock kernel/crypto kernel/lib kernel/user
```

Current result: **no direct arch macro branches found outside `kernel/arch/**`.**

Additional grep-driven findings (still architecture-coupled behavior):

| Finding | Evidence | Portability note | Bookmark |
| --- | --- | --- | --- |
| Core boot path directly depends on x86 IDT wiring | `kernel/core/kmain.c` includes `../arch/x86/idt.h` and calls `idt_init()` | Keep this dependency explicit until a per-arch interrupt bootstrap interface exists | `TODO(port): #623` |
| Launcher runtime links against x86 fault-recovery implementation | `kernel/user/launcher_exec.c` includes `../arch/x86/idt.h` and uses fault-recovery entrypoints | A non-x86 port needs an architecture-neutral recover hook (or a launcher-local abstraction) | `TODO(port): #623` |
| Several drivers still use direct x86 I/O instructions (`inb/outb`) | `kernel/drivers/{serial,input,clock,disk,video,network}/*` | Driver backends are expected to stay hardware-specific, but arch-specific instruction usage should remain isolated from policy/control-plane code | `TODO(port): #623` |
| VGA text memory address assumptions remain hardcoded | `kernel/drivers/video/vga_text.c` and `kernel/drivers/video/vga_gfx.c` reference `0xB8000` | Acceptable for x86-first bootstrap; future ports require alternate video backends via `video_hal` | `TODO(port): #623` |

## 3) HAL contract checklist for future ports

A second architecture/board port should provide the same HAL-facing behavior:

- `kernel/hal/serial_hal.h`
  - register backend, init/read/write char/string, readiness + backend metadata
- `kernel/hal/storage_hal.h`
  - register backend, read/write block, capacity metadata
- `kernel/hal/video_hal.h`
  - register backend, init/clear/write/write_color, readiness + backend metadata
- `kernel/hal/network_hal.h`
  - register backend, frame send/recv, MAC retrieval, readiness + backend metadata
- `kernel/hal/clock_hal.h`
  - register backend, read wall clock (`hal_time_t`), readiness + backend metadata
- `kernel/hal/input_hal.h`
  - init + non-blocking character poll
- `kernel/hal/mouse_hal.h`
  - init/availability, state + event polling, bounds control

## 4) Portability drift-gate cadence

- Gate command: `./build/scripts/test.sh validate_no_arch_macros_outside_arch_tree`
- Implementation:
  - shell wrapper: `build/scripts/validate_no_arch_macros_outside_arch_tree.sh`
  - PowerShell peer: `build/scripts/validate_no_arch_macros_outside_arch_tree.ps1`
  - validator: `tools/validate_no_arch_macros_outside_arch_tree.py`
- Allowlist path: `build/scripts/.arch_macro_allowlist`
  - Only temporary exceptions belong there.
  - Every allowlist path must be documented in this section with
    `TODO(port): #<issue>`.

Re-audit trigger:
- any new hardware backend,
- any kernel subsystem touching boot/interrupt/process-launch paths,
- any issue proposing non-x86 architecture support.
