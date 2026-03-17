/**
 * @file vga.c
 * @brief Legacy VGA compatibility facade.
 *
 * Purpose:
 *   Preserves the historical vga_* API while delegating behavior to the
 *   architecture-agnostic video HAL and selected backend driver.
 *
 * Interactions:
 *   - drivers/video/vga_text.c: default text-mode backend for x86.
 *   - hal/video_hal.c: backend registration and dispatch.
 *   - core code may still include this facade during migration.
 *
 * Launched by:
 *   Called by boot and console paths as needed. Not a standalone process;
 *   compiled into kernel image.
 */

#include "vga.h"

#include "../../drivers/video/vga_text.h"
#include "../../hal/video_hal.h"

void vga_clear(void) {
  if (!video_hal_ready()) {
    (void)vga_text_init_primary();
  }

  video_hal_clear();
}

void vga_write(const char *s) {
  if (!video_hal_ready()) {
    (void)vga_text_init_primary();
  }

  video_hal_write(s);
}
