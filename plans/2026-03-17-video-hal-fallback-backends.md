# Video HAL Fallback Backends Plan (2026-03-17)

## Goal
Add additional video HAL backends to demonstrate multi-backend selection and provide non-VGA fallback paths for constrained or non-PC targets.

## Scope
- Add framebuffer-style text stub backend.
- Add GPIO-style text stub backend.
- Integrate boot fallback chain in kmain.
- Keep platform build scripts (ps1/sh) synchronized.

## Design
1. Framebuffer Text Stub
- Maintain an in-memory 80x25 character shadow buffer.
- Implement HAL hooks: init, clear, write.
- Register backend name `framebuffer-stub`.

2. GPIO Text Stub
- Maintain a ring buffer as simulated GPIO text sink.
- Implement HAL hooks: init, clear, write.
- Register backend name `gpio-text-stub`.

3. Boot Fallback Selection
- Try VGA backend first.
- If VGA backend init fails, try framebuffer stub.
- If framebuffer stub init fails, try GPIO stub.

## Validation
- Build kernel entry/link path via `build/scripts/build_kernel_entry.ps1`.
- Confirm shell build script contains matching object compile/link entries.

## Follow-up
- Replace framebuffer stub with real linear framebuffer text rendering.
- Replace GPIO stub with board-specific GPIO/peripheral display transport.
