# Window Manager Mouse Cursor Performance

**Date:** 2026-05-28  
**Status:** Research / Proposed  
**Issue:** Mouse cursor in `win.bin` is extremely slow/laggy when running under QEMU

---

## Root Cause Analysis

The slow mouse is caused by multiple compounding factors:

### 1. Compositor Per-Row VFB Reads (Primary Bottleneck)

`compositor.c:compositor_render()` reads the session VFB **one row at a time** via
`os_session_read_framebuffer()` — for a 256×160 VFB, that's **160 function-pointer
calls per window per frame**. Each call traverses the app→bridge→session_manager
path. This dominates frame time and limits the effective frame rate to single-digit
Hz, meaning mouse position is only sampled once every 100–200ms.

**Location:** `user/apps/win/compositor.c:102-117`

### 2. QEMU Display/Input Configuration

The graphical QEMU args (`build/qemu/x86_64-graphical.args`) lack:
- **`-device usb-tablet`** — an absolute-positioning input device that eliminates
  relative-motion drift and cursor sync issues.
- **`-vga std` or `-device virtio-vga`** — explicit graphics device selection for
  better emulated VGA performance.
- **`-enable-kvm`** (on Linux hosts) — hardware virtualization drastically reduces
  CPU overhead, speeding up the entire guest including the render loop.
- **`-cpu host`** — when used with KVM, exposes host CPU features for faster execution.

**Location:** `build/qemu/x86_64-graphical.args`

### 3. No Frame Pacing / Yield

The main loop in `user/apps/win/main.c` has no yield or sleep between frames. While
this seems like it should make things faster, without KVM the CPU is fully consumed
by the compositor's slow memcpy loop, leaving no slack for QEMU's virtual PS/2
controller to inject mouse packets between polls.

### 4. PS/2 Mouse Sample Rate

The driver sets 100 samples/sec (`kernel/drivers/input/ps2_mouse.c:140`), which is
adequate. The issue is not the hardware rate but how infrequently the WM polls it
(once per frame, where frames are slow).

### 5. Mouse HAL Drain Fix Already Present

`kernel/hal/mouse_hal.c` already has the `MOUSE_DRAIN_MAX=64` fix (#337) that drains
accumulated bytes each call. This helps but doesn't solve the root cause of slow frames.

---

## Recommended Fixes (Priority Order)

### A. Bulk VFB Read Syscall (Highest Impact)

Add a bulk `os_session_read_framebuffer()` variant that reads the **entire VFB** (or
a rectangular region) in a single call, returning all pixel data at once. This reduces
160 bridge calls → 1.

```c
// New API: read full rectangle in one call
os_status_t os_session_read_framebuffer_rect(
    unsigned int session_id,
    unsigned char *out_pixels,
    unsigned int x, unsigned int y,
    unsigned int w, unsigned int h
);
```

The existing `session_manager_read_vfb()` already accepts w×h — the compositor just
needs to call it once with the full height instead of row-by-row.

**Estimated impact:** 10–50× frame rate improvement.

### B. QEMU `-device usb-tablet` (High Impact, Zero Code Change)

Add to `build/qemu/x86_64-graphical.args`:
```
-usb
-device usb-tablet
```

This gives QEMU absolute mouse positioning, eliminating sync drift and providing
smoother coordinate delivery to the guest PS/2 emulation layer.

**Note:** The guest still sees a PS/2 mouse (QEMU translates tablet→PS/2 for legacy
guests), so no kernel driver changes needed.

### C. Add `-enable-kvm` When Available (High Impact on Linux)

On Linux hosts, adding `-enable-kvm -cpu host` provides hardware virtualization,
making the entire guest run at near-native speed. This won't help on Windows hosts
(use `-accel whpx` or `-accel hax` instead).

Suggested approach: detect platform in launch scripts and add the appropriate
accelerator flag.

### D. Decouple Mouse Polling from Rendering

Poll mouse more frequently than rendering. Options:
1. **Poll mouse multiple times per frame** — call `os_mouse_get_state()` in a tight
   sub-loop before compositing, or
2. **Separate mouse update from compositor** — update cursor position independently
   of VFB compositing (hardware cursor approach)
3. **Dirty-rect rendering** — only re-render changed regions, dramatically reducing
   per-frame work

### E. Increase PS/2 Sample Rate to 200

Change `ps2_mouse.c:140` from `100` to `200`. Modern mice support 200 samples/sec
and this doubles the resolution of motion data available per drain cycle.

### F. VGA Hardware Cursor (Eliminates Cursor Lag Entirely)

Use VGA hardware cursor registers (or a sprite overlay) so the cursor moves
independently of framebuffer composition. The cursor position update becomes a
single register write regardless of frame rate.

---

## Quick Win Summary

| Fix | Effort | Impact | Code Changes |
|-----|--------|--------|-------------|
| Bulk VFB read | Medium | Very High | compositor.c, launcher_exec.c, secureos_api |
| usb-tablet in QEMU args | Trivial | High | x86_64-graphical.args only |
| -enable-kvm | Trivial | High | Launch scripts only |
| Poll mouse N times/frame | Low | Medium | main.c |
| Sample rate → 200 | Trivial | Low | ps2_mouse.c |
| Hardware cursor | High | Very High | New VGA cursor driver |

---

## Immediate Actions

1. Add `-usb` and `-device usb-tablet` to `x86_64-graphical.args`
2. Change compositor to read full VFB in one call (the infra already supports it)
3. Bump PS/2 sample rate to 200
