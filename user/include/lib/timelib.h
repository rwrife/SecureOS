#ifndef SECUREOS_TIMELIB_H
#define SECUREOS_TIMELIB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  TIMELIB_STATUS_OK = 0,
  TIMELIB_STATUS_NOT_READY = 1,
  TIMELIB_STATUS_DENIED = 2,
  TIMELIB_STATUS_ERROR = 3,
} timelib_status_t;

typedef struct {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t weekday; /* 1=Sun, 2=Mon, ..., 7=Sat */
} timelib_datetime_t;

/**
 * Get the current date and time.
 */
timelib_status_t timelib_now(timelib_datetime_t *out);

/**
 * Get seconds since 2000-01-01 00:00:00 UTC.
 */
timelib_status_t timelib_epoch(unsigned int *out);

/**
 * Get monotonic tick count since boot.
 */
timelib_status_t timelib_ticks(unsigned int *out);

/**
 * Format datetime as "YYYY-MM-DD HH:MM:SS".
 * Buffer must be at least 20 bytes.
 */
timelib_status_t timelib_format(const timelib_datetime_t *t, char *buf,
                                unsigned int buf_size);

/**
 * Format time only as "HH:MM:SS".
 * Buffer must be at least 9 bytes.
 */
timelib_status_t timelib_format_time(const timelib_datetime_t *t, char *buf,
                                     unsigned int buf_size);

/**
 * Format date only as "YYYY-MM-DD".
 * Buffer must be at least 11 bytes.
 */
timelib_status_t timelib_format_date(const timelib_datetime_t *t, char *buf,
                                     unsigned int buf_size);

/**
 * Get weekday name (e.g. "Monday").
 * Buffer must be at least 10 bytes.
 */
timelib_status_t timelib_day_name(uint8_t weekday, char *buf,
                                  unsigned int buf_size);

#ifdef __cplusplus
}
#endif

#endif
