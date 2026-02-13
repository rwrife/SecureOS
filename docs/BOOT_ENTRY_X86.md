# x86 Boot Entry (Initial)

This introduces a minimal x86 boot-entry handoff scaffold:

- `kernel/arch/x86/boot/entry.asm` — `_start` entry and stack setup
- `kernel/arch/x86/boot/linker.ld` — linker layout + entrypoint at 1 MiB
- `kernel/core/kmain.c` — `kmain` handoff target with early test markers
- `kernel/arch/x86/serial.c` + `serial.h` — COM1 early serial driver

## Build

```bash
./build/scripts/build_kernel_entry.sh
```

## Artifacts

- `artifacts/kernel/kernel.elf`
- `artifacts/kernel/kernel.map`
- `artifacts/kernel/kernel.sections.txt`

## Notes

This is an initial handoff scaffold for issue #7. It validates assembly/linker integration and symbol handoff to `kmain`.
QEMU boot integration for this ELF path is handled in follow-up tasks.
