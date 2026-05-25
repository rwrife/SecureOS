/**
 * @file boot_banner.c
 * @brief Renders a colorful ASCII-art boot banner during kernel startup.
 *
 * Purpose:
 *   Displays the SecureOS splash screen with an SOS logo and version
 *   info on the VGA display. Serial output receives a plain-text
 *   representation so boot logs remain readable without color codes.
 *
 * Interactions:
 *   - hal/video_hal.c: video_hal_write_color() for colored VGA output.
 *   - hal/serial_hal.c: plain text boot log.
 *   - core/version.h: provides SECUREOS_VERSION string.
 *
 * Launched by:
 *   Called from kmain() after the video HAL is initialized.
 *   Not a standalone process; compiled into the kernel image.
 */

#include "boot_banner.h"
#include "version.h"
#include "vga_colors.h"
#include "../hal/serial_hal.h"
#include "../hal/video_hal.h"

#define SOS_COLOR VGA_ATTR(VGA_YELLOW, VGA_BLACK)

void boot_banner_display(void) {
  video_hal_write("\n");
  serial_hal_write("\n");

  video_hal_write_color("   ___  ___  ___", SOS_COLOR);
  video_hal_write("\n");
  serial_hal_write("   ___  ___  ___\n");

  video_hal_write_color("  / __|| _ \\/ __|", SOS_COLOR);
  video_hal_write_color("    SecureOS v" SECUREOS_VERSION "\n",
                        VGA_ATTR(VGA_WHITE, VGA_BLACK));
  serial_hal_write("  / __|| _ \\/ __|    SecureOS v" SECUREOS_VERSION "\n");

  video_hal_write_color("  \\__ \\|  / \\__ \\", SOS_COLOR);
  video_hal_write_color("    Zero-Trust OS\n",
                        VGA_ATTR(VGA_LIGHT_GREY, VGA_BLACK));
  serial_hal_write("  \\__ \\|  / \\__ \\    Zero-Trust OS\n");

  video_hal_write_color("  |___/|_\\_\\|___/", SOS_COLOR);
  video_hal_write("\n\n");
  serial_hal_write("  |___/|_\\_\\|___/\n\n");
}
