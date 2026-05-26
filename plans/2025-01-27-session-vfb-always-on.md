# Per-Session Graphics Buffer (Always-On VFB)

**Date**: 2025-01-27
**Status**: In Progress

## Problem

Currently the window manager has two rendering paths:
- **Text mode**: WM reads `session_read_output()` text, stores in `win_window_t.text[][]`, WM renders bitmap font itself
- **Graphics mode**: WM reads session VFB pixels via `session_read_framebuffer()`

This is overly complex. The user's model is simpler: **every session always has a graphics buffer**. The WM just reads pixels — it doesn't care if the session is doing text or graphics.

## Solution

1. **Every WM-managed session gets a VFB on creation** (no lazy allocation, no `gfx_mode` check)
2. **Kernel renders console text into the VFB** for WM-managed sessions (a simple kernel-side bitmap font renderer draws characters into the pixel buffer as they're printed)
3. **WM compositor always reads pixels** — remove the text-mode rendering path from the compositor
4. **Apps that do graphics** just write pixels to the same VFB via `put_pixel`/`blit` (already works)

## Architecture

```
Session (WM-managed)
├── VFB (320×200 pixel buffer, always allocated)
├── Console output → kernel text renderer → VFB pixels
└── App graphics (put_pixel/blit) → VFB pixels directly

Window Manager
├── Reads VFB pixels for each session
├── Composites windows onto physical framebuffer
└── No text rendering needed in WM itself
```

## Changes

### Kernel Side
- `session_manager.c`: Allocate VFB immediately in `set_wm_managed(sid, 1)`
- `session_manager.c`: Add `session_vfb_putchar()` — renders a character glyph into the VFB at the session's cursor position, advances cursor
- `session_manager.c`: Add `session_vfb_write()` — renders a string into VFB (calls putchar per char)
- `session_manager.c`: Track per-session text cursor (col, row) for VFB text rendering
- Console/output path: When writing to a WM-managed session's screen_history, also render into VFB
- Add `kernel/gfx/vfb_font.c` — minimal 5×7 bitmap font for kernel-side VFB text rendering

### User Side (WM)
- `compositor.c`: Remove text-mode branch, always read VFB pixels
- `window.h`: Remove `text[][]` buffer and cursor tracking (no longer needed)
- `main.c`: Remove `os_session_read_output()` polling loop (no longer needed)

## Benefits
- Simpler WM code — just reads pixels
- Unified model — every session is a graphics buffer
- Apps can freely mix text and graphics in the same session
- Easy to extend to other HAL buffers (audio, etc.) later
