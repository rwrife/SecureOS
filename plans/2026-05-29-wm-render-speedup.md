# Window Manager Render Speedup

**Date:** 2026-05-29
**Status:** Implemented
**Branch:** `perf/wm-render-speedup`
**Follow-up to:** `plans/2026-05-28-wm-mouse-performance.md`

---

## Problem

After landing the bulk VFB read optimization in `2026-05-28`, the window
manager mouse and window dragging were still extremely laggy. Running `draw`
standalone (outside the WM) is smooth — confirming the bottleneck is the WM
render pipeline, not the input or the underlying VGA driver.

## Root cause

Profiling the WM render path against the standalone `draw` app revealed two
remaining hot loops, both running at `-O0` (kernel and user binaries are
compiled freestanding without `-O2`):

### 1. Kernel: per-pixel `vga_gfx_put_pixel` calls (primary)

`app_native_video_blit()` in `kernel/user/launcher_exec.c` flushed the WM
backbuffer to physical VGA via:

```c
for (row = 0; row < h; row++)
  for (col = 0; col < w; col++)
    vga_gfx_put_pixel(x + col, y + row, pixels[row * w + col]);
```

For a full 320×200 backbuffer flush that is **64,000 cross-translation-unit
function calls per frame**, and each `vga_gfx_put_pixel` performs four
bounds checks before a single byte write. At `-O0` this dominates frame
time.

The same pattern exists in the WM-managed VFB destination path inside the
same function.

### 2. Compositor: per-pixel bounds-checked loops

`compositor.c` rendered each visible rectangle via per-pixel bounds-checked
loops:

- `fill_rect()` checked `px/py` per pixel — ~64K iterations per full-screen
  fill, ~40K for the content area, plus smaller rects for title bar,
  border, close button, Quit button.
- The VFB-to-backbuffer copy in `draw_window()` did the same — ~40K
  iterations per window per frame.

## Fix

### Kernel

- Add `vga_gfx_blit(x, y, w, h, pixels)` in `kernel/drivers/video/vga_gfx.c`.
  It clips the destination rectangle to screen bounds once, then walks one
  row at a time and writes the clipped span via a tight pointer loop. The
  destination pointer remains `volatile` so the compiler treats VGA MMIO
  writes conservatively.
- Replace the per-pixel double loop in `app_native_video_blit()` with a
  single call to `vga_gfx_blit()` on the real-hardware path. This is the
  single largest win: 64,000 function calls per frame → 200 inline row
  copies.
- Apply the same clipped row-copy pattern to the WM-managed VFB destination
  branch of `app_native_video_blit()`, picking up the actual VFB dimensions
  from `session_manager_get_vfb_size()` (previously the loop hard-coded
  320/200 even when the session VFB was 256×160).

### Compositor (`user/apps/win/compositor.c`)

- Clip rectangles once at the top of `fill_rect()` and walk each clipped
  row with a tight pointer loop instead of doing per-pixel bounds checks.
- Apply the same per-row clip + tight inner loop to the VFB-to-backbuffer
  copy in `draw_window()`.

## Out of scope (deferred)

- Adding `-O2` to the kernel/user build flags. That would amplify these
  wins further, but it is a riskier global change. Track separately.
- VGA hardware cursor support.
- Dirty-rect compositing (only re-render windows that moved or had VFB
  changes).
- Decoupling input polling from rendering (poll mouse N times per frame).

## Verification

- Visual: mouse movement and window dragging are smooth in QEMU after the
  change; `draw` inside a window remains responsive.
- Functional: no behavior change. All existing rendering paths still draw
  the same pixels, just with bulk row copies instead of per-pixel calls.
- Builds via `scripts/build.ps1 force` and `scripts/build.sh force`.
