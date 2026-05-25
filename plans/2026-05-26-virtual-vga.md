# Virtual VGA Framebuffer Architecture

**Date**: 2026-05-26
**Status**: In Progress
**Related**: 2026-05-25-window-manager.md

## Problem

Currently each window in the WM renders text output from the session's `screen_history` buffer using a bitmap font. This works for text-mode sessions, but when a session launches a graphics application (e.g. `draw.bin`), the app's pixel output goes directly to the physical VGA framebuffer, bypassing the WM's compositing.

The WM should be a wrapper around the console: it lets users view multiple sessions via windows. Each session may be text-mode (console) or graphics-mode (an app that calls `os_video_set_mode(GFX)` and draws pixels). In both cases, the window should display the session's visual output.

## Solution: Per-Session Virtual Framebuffer

Each WM-managed session gets a **virtual framebuffer** — a 320×200 pixel buffer that lives in the kernel `session_record_t`. When an app in that session calls video operations (`put_pixel`, `blit`, `draw_rect`, `set_mode`), the kernel redirects those operations to the session's virtual framebuffer instead of the physical VGA hardware.

The WM then reads the session's virtual framebuffer and composites the relevant portion into its window.

### Text-Mode Sessions

For text-mode sessions, the console output still goes to `screen_history`. The WM continues to render text into the window content area using its bitmap font, as it does now. The virtual framebuffer is only used when the session is in graphics mode.

Alternatively (future enhancement), the kernel could render console text into the virtual framebuffer directly, so the WM always just blits the framebuffer. For now, we keep the dual path: text = font rendering, graphics = framebuffer blit.

### Graphics-Mode Sessions

When a session app calls `os_video_set_mode(GFX)`:
1. The kernel marks the session as "graphics mode" instead of switching the physical VGA
2. All subsequent `put_pixel`/`blit`/`draw_rect` calls write to the session's virtual framebuffer
3. `set_mode(TEXT)` marks the session back to text mode

The WM detects that a window's session is in graphics mode and blits the virtual framebuffer directly into the window content area (scaled/clipped to fit).

## Kernel Changes

### 1. Virtual Framebuffer in Session (`session_manager.h/c`)

```c
#define SESSION_VFB_WIDTH  320
#define SESSION_VFB_HEIGHT 200
#define SESSION_VFB_SIZE   (SESSION_VFB_WIDTH * SESSION_VFB_HEIGHT)

typedef struct {
  int in_use;
  unsigned int session_id;
  cap_subject_id_t subject_id;
  console_context_t console_context;
  /* Virtual framebuffer for WM-managed sessions */
  int gfx_mode;  /* 0 = text, 1 = graphics */
  unsigned char vfb[SESSION_VFB_SIZE]; /* 320x200 pixel buffer */
} session_record_t;
```

### 2. Redirect Video Bridge Calls (`launcher_exec.c`)

When a WM-managed session app calls video functions:
- `video_set_mode(GFX)`: set `session.gfx_mode = 1`, do NOT switch physical VGA
- `video_put_pixel(x, y, color)`: write to `session.vfb[y*320+x]`
- `video_blit(x, y, w, h, pixels)`: copy block into `session.vfb`
- `video_draw_rect(x, y, w, h, color)`: fill region in `session.vfb`
- `video_set_mode(TEXT)`: set `session.gfx_mode = 0`

Non-WM-managed sessions (session 0 / the physical console) continue using the real VGA hardware.

### 3. New Syscall: Read Virtual Framebuffer

```c
os_status_t os_session_read_framebuffer(unsigned int session_id,
                                        unsigned char *out_pixels,
                                        unsigned int x, unsigned int y,
                                        unsigned int w, unsigned int h);
```

The WM calls this to grab a region of the session's virtual framebuffer.

### 4. Query Graphics Mode

```c
os_status_t os_session_get_gfx_mode(unsigned int session_id, int *out_mode);
```

The WM checks whether to render text or blit pixels for each window.

## WM Changes

### Window Rendering Logic

```
for each window:
  if session is in graphics mode:
    read virtual framebuffer region
    blit pixels into window content area
  else:
    read screen_history text
    render text with bitmap font (current behavior)
```

### Window Sizing

In graphics mode, the window content area ideally maps 1:1 to the virtual framebuffer (or a viewport of it). Since the physical screen is 320×200 and the window has borders/titlebar, the content area is smaller. Options:
- Scale down (lossy but fits)
- Scroll/pan (full res, user scrolls)
- For v1: just clip to the window content area dimensions

## Implementation Steps

1. Add `gfx_mode` and `vfb[]` to `session_record_t`
2. Add `session_manager_get_gfx_mode()` and `session_manager_read_vfb()` kernel APIs
3. Modify bridge video functions to redirect to vfb when session is WM-managed
4. Add `os_session_read_framebuffer` and `os_session_get_gfx_mode` to secureos_api.h
5. Wire stubs and bridge
6. Modify WM compositor to check mode and blit framebuffer for gfx sessions
7. The WM's own session (session 0) keeps using the REAL VGA (it IS the compositor)
