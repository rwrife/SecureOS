# Modern Boot UI & Console Refresh

**Date:** 2026-05-25
**Status:** Implemented (PR pending)
**Slice:** UX / Boot Experience

## Summary

Modernize the SecureOS boot sequence and console shell to feel more
polished and user-friendly:

1. **VGA color support** — extended the video HAL with
   `video_hal_write_color(msg, attr)` so kernel code can render colored
   text. The VGA text driver implements per-character attribute bytes; stub
   backends fall back to plain write.

2. **Rainbow ASCII-art boot banner** — `boot_banner_display()` renders a
   figlet-style "SecureOS" logo with each line cycling through bright VGA
   colors (red, yellow, green, cyan, blue, magenta).

3. **Version string** — `kernel/core/version.h` defines
   `SECUREOS_VERSION "0.1.0"` displayed in the banner and console
   welcome.

4. **Improved prompt** — changed from `secureos[s0]> ` to
   `[s0 /]> ` showing session number and current working directory
   (e.g. `[s1 /apps]> `).

5. **VGA scrolling** — replaced the old wrap-to-row-0 behavior with proper
   upward scrolling when the cursor reaches the bottom of the 80x25 screen.

## Files

| Action   | Path                                        |
|----------|---------------------------------------------|
| Created  | `kernel/core/version.h`                   |
| Created  | `kernel/core/vga_colors.h`                |
| Created  | `kernel/core/boot_banner.h`               |
| Created  | `kernel/core/boot_banner.c`               |
| Modified | `kernel/hal/video_hal.h`                  |
| Modified | `kernel/hal/video_hal.c`                  |
| Modified | `kernel/drivers/video/vga_text.c`         |
| Modified | `kernel/drivers/video/framebuffer_text_stub.c` |
| Modified | `kernel/drivers/video/gpio_text_stub.c`   |
| Modified | `kernel/core/kmain.c`                     |
| Modified | `kernel/core/console.c`                   |
| Modified | `build/scripts/build_kernel_entry.sh`     |
| Modified | `build/scripts/run_qemu.sh`               |

## Testing

- `kernel_console` QEMU integration test passes with new prompt and
  welcome message assertions.
- `kernel_sessions` test has a pre-existing timeout (fails on main too).
- Build succeeds cleanly with no warnings.