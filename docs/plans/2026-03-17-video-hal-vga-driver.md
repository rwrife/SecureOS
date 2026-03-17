# Video HAL + VGA Driver Plan (2026-03-17)

## Goal
Introduce a hardware-agnostic video HAL and a concrete VGA text-mode backend so kernel display logic is decoupled from architecture-specific implementation details.

## Scope
- Add `kernel/hal/video_hal.*` for backend registration and dispatch.
- Add `kernel/drivers/video/vga_text.*` as the default x86 text console backend.
- Migrate kernel core usage to video HAL calls.
- Keep legacy x86 VGA module as compatibility facade.
- Keep PowerShell and shell build scripts synchronized.

## Design
1. Video HAL Interface
- Define primary backend registration API with `init`, `clear`, and `write` hooks.
- Expose readiness and backend identity helpers.

2. VGA Text Backend
- Implement writes to text buffer at `0xB8000` (80x25, attribute `0x07`).
- Handle newline and wrap behavior with a simple cursor state.
- Register as `VIDEO_BACKEND_VGA_TEXT`.

3. Integration
- In `kmain`, initialize VGA backend via `vga_text_init_primary()` and clear through `video_hal_clear()`.
- In `console`, replace direct VGA calls with `video_hal_write()` and `video_hal_clear()`.
- Keep `kernel/arch/x86/vga.*` delegating to HAL for compatibility.

## Validation
- Run `build/scripts/build_kernel_entry.ps1` and ensure successful link.
- Confirm shell script build path mirrors object compilation and linker inputs.

## Follow-up
- Add a framebuffer backend and runtime backend selection for non-VGA platforms.
- Add a test backend for HAL unit tests without touching hardware memory.
