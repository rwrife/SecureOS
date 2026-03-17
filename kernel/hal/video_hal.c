/**
 * @file video_hal.c
 * @brief Hardware Abstraction Layer for video output devices.
 *
 * Purpose:
 *   Stores and dispatches to the active video backend so upper layers are
 *   independent of architecture-specific display hardware.
 *
 * Interactions:
 *   - drivers/video/vga_text.c: registers VGA text mode backend.
 *   - core/console.c: calls video_hal_write/clear for console rendering.
 *   - core/kmain.c: calls video_hal_init through backend bootstrap.
 *
 * Launched by:
 *   The active driver registers itself at boot. Not a standalone process;
 *   compiled into kernel image.
 */

#include "video_hal.h"

static const video_device_t *video_primary_device;
static int video_initialized;

void video_hal_reset_for_tests(void) {
  video_primary_device = 0;
  video_initialized = 0;
}

void video_hal_register_primary(const video_device_t *device) {
  video_primary_device = device;
  video_initialized = 0;
}

int video_hal_init(void) {
  if (video_primary_device == 0 || video_primary_device->init == 0) {
    return 0;
  }

  video_initialized = video_primary_device->init() ? 1 : 0;
  return video_initialized;
}

int video_hal_ready(void) {
  return video_initialized &&
         video_primary_device != 0 &&
         video_primary_device->clear != 0 &&
         video_primary_device->write != 0;
}

video_backend_t video_hal_backend(void) {
  if (!video_hal_ready()) {
    return VIDEO_BACKEND_UNKNOWN;
  }
  return video_primary_device->backend;
}

const char *video_hal_backend_name(void) {
  if (!video_hal_ready() || video_primary_device->backend_name == 0) {
    return "unknown";
  }
  return video_primary_device->backend_name;
}

void video_hal_clear(void) {
  if (!video_hal_ready()) {
    return;
  }

  video_primary_device->clear();
}

void video_hal_write(const char *message) {
  if (!video_hal_ready() || message == 0) {
    return;
  }

  video_primary_device->write(message);
}
