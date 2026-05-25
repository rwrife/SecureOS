#ifndef SECUREOS_VGA_COLORS_H
#define SECUREOS_VGA_COLORS_H

/**
 * @file vga_colors.h
 * @brief Standard 16-color VGA text-mode palette constants.
 *
 * Purpose:
 *   Provides named constants for the 16 VGA text-mode foreground/background
 *   colors and a macro to compose a full attribute byte.
 *
 * Interactions:
 *   - core/boot_banner.c: uses colors for the rainbow boot logo.
 *   - drivers/video/vga_text.c: interprets attribute bytes.
 *
 * Launched by:
 *   Header-only; compiled into any translation unit that includes it.
 */

#include <stdint.h>

/* Foreground / background color indices (4-bit) */
#define VGA_BLACK         0x0
#define VGA_BLUE          0x1
#define VGA_GREEN         0x2
#define VGA_CYAN          0x3
#define VGA_RED           0x4
#define VGA_MAGENTA       0x5
#define VGA_BROWN         0x6
#define VGA_LIGHT_GREY    0x7
#define VGA_DARK_GREY     0x8
#define VGA_LIGHT_BLUE    0x9
#define VGA_LIGHT_GREEN   0xA
#define VGA_LIGHT_CYAN    0xB
#define VGA_LIGHT_RED     0xC
#define VGA_LIGHT_MAGENTA 0xD
#define VGA_YELLOW        0xE
#define VGA_WHITE         0xF

/* Compose a VGA attribute byte: (background << 4) | foreground */
#define VGA_ATTR(fg, bg) ((uint8_t)(((bg) << 4) | (fg)))

/* Common presets */
#define VGA_ATTR_DEFAULT  VGA_ATTR(VGA_LIGHT_GREY, VGA_BLACK)
#define VGA_ATTR_BRIGHT   VGA_ATTR(VGA_WHITE, VGA_BLACK)
#define VGA_ATTR_ERROR    VGA_ATTR(VGA_LIGHT_RED, VGA_BLACK)
#define VGA_ATTR_SUCCESS  VGA_ATTR(VGA_LIGHT_GREEN, VGA_BLACK)
#define VGA_ATTR_INFO     VGA_ATTR(VGA_LIGHT_CYAN, VGA_BLACK)

#endif