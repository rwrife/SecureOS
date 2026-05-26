# Virtual Graphics HAL for WM-managed Sessions

**Date:** 2026-05-25  
**Status:** Implemented  
**Related:** 2026-05-25-window-manager.md, 2026-05-26-virtual-vga.md

## Problem

When a graphics application (e.g. `draw.bin`) runs inside a WM-managed session,
several kernel bridge functions bypass the session's virtual framebuffer and
directly manipulate the physical VGA hardware that the window manager owns.
This causes:

1. **Black screen** — `app_native_video_clear()` calls `vga_gfx_clear(0)` on
   the real framebuffer, wiping the WM's composited output.
2. **VM crash on exit** — Post-app cleanup unconditionally calls
   `vga_gfx_leave()` when VGA mode is active, tearing down the WM's graphics
   mode.
3. **Input conflict** — `app_native_input_read_char()` reads raw keyboard
   hardware, competing with the WM for keystrokes. Mouse returns physical
   coordinates instead of window-relative virtual coordinates.

## Architecture Principle

**The session manager controls all virtual HAL buffers.** WM-managed sessions
never touch physical hardware. The bridge functions route all I/O through the
session manager's per-session state:

- **Video** → Session's VFB (320×200 pixel buffer)
- **Mouse** → Session's virtual mouse state (injected by WM)
- **Keyboard** → Session's inject buffer (populated by WM via
  `os_session_write_input`)

The window manager and console are *display consumers* — they read from the
session manager's buffers for rendering purposes.

## Changes

### `kernel/user/launcher_exec.c`
- `app_native_video_clear()`: Clears session VFB when `wm_managed`, never
  touches real VGA.
- `app_native_video_put_pixel/get_pixel/draw_rect/blit()`: Gate on
  `wm_managed` first (not just `gfx_mode`). WM-managed sessions never fall
  through to real hardware.
- `app_native_video_set_mode(TEXT)`: WM-managed sessions just clear the
  `gfx_mode` flag; never call `vga_gfx_leave()`.
- `app_native_video_get_resolution()`: Returns VFB dimensions for WM-managed
  sessions based on their own `gfx_mode`, not global VGA state.
- `app_native_video_set_cursor/putchar_at()`: No-op for WM-managed sessions.
- `app_native_mouse_get_state()`: Returns virtual mouse state for WM-managed
  sessions via `session_manager_get_virtual_mouse()`.
- `app_native_input_read_char()`: WM-managed sessions read from inject buffer
  via `console_try_read_injected()` instead of raw keyboard.
- Post-app cleanup: Skips `vga_gfx_leave()` for WM-managed sessions.

### `kernel/core/session_manager.c/h`
- Added `virtual_mouse_x/y/buttons` fields to `session_record_t`.
- Added `session_manager_set_virtual_mouse()` / `get_virtual_mouse()`.
- Added `session_manager_clear_vfb()`.

### `kernel/core/console.c/h`
- Added `console_try_read_injected()`: reads one char from inject buffer
  for graphics apps that poll `input_read_char`.

### `user/include/secureos_api.h` + `user/runtime/secureos_api_stubs.c`
- Added `os_session_set_virtual_mouse()` API.

### `user/apps/win/input.c`
- WM now injects virtual mouse state into the focused window's session
  each frame, translating physical screen coordinates to VFB-relative
  coordinates.

### Bridge struct (`app_native_bridge_t`)
- Added `session_set_virtual_mouse` function pointer.
