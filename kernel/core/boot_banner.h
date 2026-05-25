#ifndef SECUREOS_BOOT_BANNER_H
#define SECUREOS_BOOT_BANNER_H

/**
 * @file boot_banner.h
 * @brief Colorful ASCII-art boot banner for SecureOS.
 *
 * Purpose:
 *   Declares boot_banner_display() which renders a rainbow ASCII-art logo
 *   and version string to the video HAL during kernel startup.
 *
 * Interactions:
 *   - hal/video_hal.c: uses video_hal_write_color for colored output.
 *   - hal/serial_hal.c: writes plain-text fallback to serial.
 *   - core/kmain.c: calls boot_banner_display() at boot.
 *
 * Launched by:
 *   Called from kmain() after video HAL is initialized.
 *   Not a standalone process; compiled into the kernel image.
 */

void boot_banner_display(void);

#endif