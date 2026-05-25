#ifndef SECUREOS_CLOCK_HAL_H
#define SECUREOS_CLOCK_HAL_H

#include <stdint.h>

/* Unified time structure used across the HAL boundary */
typedef struct {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t weekday; /* 1=Sun, 2=Mon, ..., 7=Sat */
} hal_time_t;

typedef enum {
  CLOCK_OK = 0,
  CLOCK_ERR_NOT_READY,
  CLOCK_ERR_INVALID,
  CLOCK_ERR_BUSY,
  CLOCK_ERR_DENIED,
} clock_result_t;

typedef enum {
  CLOCK_BACKEND_NONE = 0,
  CLOCK_BACKEND_CMOS_RTC,
} clock_backend_t;

typedef clock_result_t (*clock_read_fn_t)(hal_time_t *out);

/* Clock device descriptor (vtable pattern matching storage_hal) */
typedef struct {
  clock_backend_t backend;
  const char *backend_name;
  clock_read_fn_t read_time;
} clock_device_t;

clock_result_t clock_hal_register(const clock_device_t *dev);
clock_result_t clock_hal_read_time(hal_time_t *out);
int clock_hal_is_ready(void);
const char *clock_hal_get_backend_name(void);

#endif
