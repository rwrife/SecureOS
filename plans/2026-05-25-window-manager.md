# Window Manager (`win.bin`)

**Date:** 2026-05-25  
**Status:** Implementation in progress

## Summary

A simple compositing window manager for SecureOS. Each "window" is a
terminal session rendered into a private framebuffer. The WM composites
all window buffers onto the physical VGA mode-13h screen (320×200×256),
handles mouse-driven drag/focus, and routes keyboard input to the
focused window.

## Goals

1. Initialize VGA graphics mode and mouse.
2. Maintain per-window framebuffers (text buffers rendered with a bitmap font).
3. Composite windows back-to-front with title bars onto a back-buffer.
4. Support click-to-focus and title-bar drag to move windows.
5. Route keyboard input to the focused window's session.
6. Provide a foundation for future graphics-app passthrough (virtual framebuffers).

## Architecture

```
┌──────────────────────────────────────────────────────┐
│  Physical VGA Framebuffer (320×200, mode 13h)        │
│  ┌────────────────────┐  ┌────────────────────┐     │
│  │▓▓ Session 1 ▓▓▓▓▓▓│  │░░ Session 2 ░░░░░░│     │
│  │ Terminal text      │  │ Terminal text      │     │
│  │ rendered via font  │  │ rendered via font  │     │
│  └────────────────────┘  └────────────────────┘     │
│                        ▲ cursor                      │
└──────────────────────────────────────────────────────┘
```

## Kernel/User API Additions

### Video Blit (performance)

```c
os_status_t os_video_blit(int x, int y, int w, int h, const unsigned char *pixels);
```

Bulk-copies a pixel buffer to the VGA framebuffer starting at (x, y).
Without this, flushing 64 000 pixels per frame via `os_video_put_pixel`
one-at-a-time is prohibitively slow.

### Session Management (user-level)

```c
os_status_t os_session_create(unsigned int *out_session_id);
os_status_t os_session_read_output(unsigned int session_id, char *buf, unsigned int buf_size, unsigned int *out_len);
os_status_t os_session_write_input(unsigned int session_id, const char *input, unsigned int len);
```

These expose the existing kernel session manager to user-space so the WM
can create child sessions and shuttle I/O between them and its windows.

## Application Structure

```
user/apps/win/
├── main.c          – entry, event loop, lifecycle
├── window.h/.c     – window struct, create/destroy, hit-testing
├── compositor.h/.c – back-buffer, draw windows, flush
├── titlebar.h/.c   – title bar rendering (text, close btn)
├── input.h/.c      – mouse dispatch (drag, focus), keyboard routing
└── font.h/.c       – 5×7 bitmap font data + render helpers
```

## Data Structures

```c
#define WIN_MAX_WINDOWS    4
#define WIN_TITLE_HEIGHT   10   /* pixels */
#define WIN_CONTENT_COLS   36
#define WIN_CONTENT_ROWS   18
#define WIN_CHAR_W         5
#define WIN_CHAR_H         7

typedef struct {
    int active;
    int x, y;
    int width, height;
    char title[32];
    unsigned int session_id;
    char text[WIN_CONTENT_ROWS][WIN_CONTENT_COLS + 1];
    int cursor_col, cursor_row;
    int focused;
    int z_order;
} win_window_t;
```

## Event Loop

1. Enter graphics mode, init mouse.
2. Create window → create session.
3. Loop:
   - Poll mouse: update position, detect drag/focus clicks.
   - Poll keyboard: send to focused session via `os_session_write_input`.
   - Read session output into window text buffers.
   - Compositor: clear back-buffer → draw windows (title + content) → draw cursor → blit to screen.
4. On ESC (or all windows closed): restore text mode, exit.

## Build Integration

- Modify `build_user_app.sh` to compile **all** `.c` files in an app
  directory (not just `main.c`).
- Add `build_user_app.sh win` to `build/scripts/build.sh`.
- Add `artifacts/user/win.bin=/apps/win.bin` to disk image mappings.
- Keep `.ps1` / `.sh` scripts in sync.

## Phase 1 (v1 — this PR)

- Stubbed session APIs (WM creates windows but I/O is mocked).
- Working compositor, font, mouse cursor, title bar, drag, focus.
- Single window on screen demonstrating the pipeline end-to-end.
- `os_video_blit` added (stub in user runtime, implemented in kernel bridge).

## Phase 2 (future)

- Wire real session I/O through kernel.
- Virtual framebuffer registration for graphics apps in windows.
- Window resize, minimize/maximize, spawn via hotkey.
- Scroll buffer for terminal history.
