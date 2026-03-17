#ifndef SECUREOS_VIDEO_HAL_H
#define SECUREOS_VIDEO_HAL_H

/**
 * @file video_hal.h
 * @brief Hardware Abstraction Layer for video output devices.
 *
 * Purpose:
 *   Defines a backend-neutral text output interface so kernel code can
 *   render console output without depending on architecture-specific video
 *   implementations. Concrete drivers (VGA text, framebuffer, GPIO display)
 *   register a primary backend during boot.
 *
 * Interactions:
 *   - drivers/video/vga_text.c: registers standard VGA text backend.
 *   - core/console.c: renders output through video_hal_write/clear.
 *   - core/kmain.c: initializes default video backend at boot.
 *
 * Launched by:
 *   A concrete video driver calls video_hal_register_primary() during
 *   startup. Not a standalone process; compiled into kernel image.
 */

typedef enum {
  VIDEO_BACKEND_UNKNOWN = 0,
  VIDEO_BACKEND_VGA_TEXT = 1,
  VIDEO_BACKEND_FRAMEBUFFER_STUB = 2,
  VIDEO_BACKEND_GPIO_TEXT_STUB = 3,
} video_backend_t;

typedef int (*video_init_fn_t)(void);
typedef void (*video_clear_fn_t)(void);
typedef void (*video_write_fn_t)(const char *message);

typedef struct {
  video_backend_t backend;
  const char *backend_name;
  video_init_fn_t init;
  video_clear_fn_t clear;
  video_write_fn_t write;
} video_device_t;

void video_hal_reset_for_tests(void);
void video_hal_register_primary(const video_device_t *device);
int video_hal_init(void);
int video_hal_ready(void);
video_backend_t video_hal_backend(void);
const char *video_hal_backend_name(void);
void video_hal_clear(void);
void video_hal_write(const char *message);

#endif
