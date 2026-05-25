/**
 * @file boot_banner.c
 * @brief Renders a colorful ASCII-art boot banner during kernel startup.
 *
 * Purpose:
 *   Displays the SecureOS splash screen with a padlock icon and version
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

/*
 * Splash layout: padlock on the left, title and tagline on the right.
 * Each line has a color attribute for the VGA display.
 */
static const char *splash_lines[] = {
  "   .-------.",
  "   | .-. .-.|",
  "   | | | | ||    SecureOS v" SECUREOS_VERSION,
  "   |_'-' '-'|    Zero-Trust OS",
  "   |  ___   |",
  "   | |   | ||",
  "   | | O | ||",
  "   | |___| ||",
  "   |_______|",
};

static const uint8_t splash_colors[] = {
  VGA_ATTR(VGA_LIGHT_CYAN,    VGA_BLACK),
  VGA_ATTR(VGA_LIGHT_CYAN,    VGA_BLACK),
  VGA_ATTR(VGA_WHITE,         VGA_BLACK),
  VGA_ATTR(VGA_LIGHT_GREY,    VGA_BLACK),
  VGA_ATTR(VGA_LIGHT_CYAN,    VGA_BLACK),
  VGA_ATTR(VGA_LIGHT_CYAN,    VGA_BLACK),
  VGA_ATTR(VGA_YELLOW,        VGA_BLACK),
  VGA_ATTR(VGA_LIGHT_CYAN,    VGA_BLACK),
  VGA_ATTR(VGA_LIGHT_CYAN,    VGA_BLACK),
};

#define SPLASH_LINE_COUNT (sizeof(splash_lines) / sizeof(splash_lines[0]))

void boot_banner_display(void) {
  unsigned int i;

  video_hal_write("\n");
  serial_hal_write("\n");

  for (i = 0; i < SPLASH_LINE_COUNT; ++i) {
    video_hal_write_color(splash_lines[i], splash_colors[i]);
    video_hal_write("\n");
    serial_hal_write(splash_lines[i]);
    serial_hal_write("\n");
  }

  video_hal_write("\n");
  serial_hal_write("\n");
}
