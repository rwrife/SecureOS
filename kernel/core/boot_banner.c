/**
 * @file boot_banner.c
 * @brief Renders a colorful ASCII-art boot banner during kernel startup.
 *
 * Purpose:
 *   Displays the SecureOS logo as rainbow-colored ASCII art on the VGA
 *   display, followed by the version number and a short tagline. Serial
 *   output receives a plain-text representation so boot logs remain
 *   readable without color codes.
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

/* Rainbow color cycle for each line of the ASCII art */
static const uint8_t rainbow_colors[] = {
  VGA_ATTR(VGA_LIGHT_RED,     VGA_BLACK),
  VGA_ATTR(VGA_YELLOW,        VGA_BLACK),
  VGA_ATTR(VGA_LIGHT_GREEN,   VGA_BLACK),
  VGA_ATTR(VGA_LIGHT_CYAN,    VGA_BLACK),
  VGA_ATTR(VGA_LIGHT_BLUE,    VGA_BLACK),
  VGA_ATTR(VGA_LIGHT_MAGENTA, VGA_BLACK),
};

#define RAINBOW_COUNT (sizeof(rainbow_colors) / sizeof(rainbow_colors[0]))

/*
 * ASCII art banner -- each line is rendered in the next rainbow color.
 * Font style: blocky / small for 80-column VGA text mode.
 */
static const char *banner_lines[] = {
  "  ____                           ___  ____  ",
  " / ___|  ___  ___ _   _ _ __ ___|_ _|/ ___| ",
  " \\___ \\ / _ \\/ __| | | | '__/ _ \\| | \\___ \\ ",
  "  ___) |  __/ (__| |_| | | |  __/| |  ___) |",
  " |____/ \\___|\\___|\\_  _|_|  \\___|___|/____/ ",
  "                                             ",
};

#define BANNER_LINE_COUNT (sizeof(banner_lines) / sizeof(banner_lines[0]))

void boot_banner_display(void) {
  unsigned int i;

  /* Blank line before banner */
  video_hal_write("\n");
  serial_hal_write("\n");

  /* Render each line in a cycling rainbow color */
  for (i = 0; i < BANNER_LINE_COUNT; ++i) {
    uint8_t color = rainbow_colors[i % RAINBOW_COUNT];
    video_hal_write_color(banner_lines[i], color);
    video_hal_write("\n");
    serial_hal_write(banner_lines[i]);
    serial_hal_write("\n");
  }

  /* Version line in bright white */
  video_hal_write_color("  SecureOS v" SECUREOS_VERSION, VGA_ATTR_BRIGHT);
  video_hal_write_color("  --  Zero-Trust Capability OS\n",
                        VGA_ATTR(VGA_LIGHT_GREY, VGA_BLACK));
  video_hal_write("\n");

  serial_hal_write("  SecureOS v" SECUREOS_VERSION
                   "  --  Zero-Trust Capability OS\n\n");
}