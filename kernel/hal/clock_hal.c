/**
 * @file clock_hal.c
 * @brief Clock hardware abstraction layer – register/read pattern for clock backends.
 *
 * Purpose:
 *   Provides architecture-independent clock access by storing a registered
 *   clock device descriptor and dispatching read operations through its
 *   function pointer vtable. Follows the same pattern as storage_hal.c.
 *
 * Interactions:
 *   - drivers/clock/cmos_rtc.c: registers as the default clock backend.
 *   - clock/clock_service.c: reads time through clock_hal_read_time().
 *   - kmain.c: indirectly used after cmos_rtc_init() registers a backend.
 *
 * Launched by:
 *   Not a standalone process. Compiled into the kernel image.
 */

#include "clock_hal.h"

static const clock_device_t *g_clock_device;

clock_result_t clock_hal_register(const clock_device_t *dev) {
  if (dev == 0) {
    return CLOCK_ERR_INVALID;
  }
  if (g_clock_device != 0) {
    return CLOCK_ERR_BUSY;
  }
  g_clock_device = dev;
  return CLOCK_OK;
}

clock_result_t clock_hal_read_time(hal_time_t *out) {
  if (g_clock_device == 0 || g_clock_device->read_time == 0) {
    return CLOCK_ERR_NOT_READY;
  }
  return g_clock_device->read_time(out);
}

int clock_hal_is_ready(void) {
  return g_clock_device != 0;
}

const char *clock_hal_get_backend_name(void) {
  if (g_clock_device == 0) {
    return "none";
  }
  return g_clock_device->backend_name;
}
