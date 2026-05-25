#ifndef SECUREOS_CLOCK_SERVICE_H
#define SECUREOS_CLOCK_SERVICE_H

#include "../hal/clock_hal.h"
#include "../cap/capability.h"

#define CLOCK_FMT_DATETIME_SIZE 20 /* "YYYY-MM-DD HH:MM:SS\0" */
#define CLOCK_FMT_TIME_SIZE 9      /* "HH:MM:SS\0" */
#define CLOCK_FMT_DATE_SIZE 11     /* "YYYY-MM-DD\0" */
#define CLOCK_FMT_DAY_SIZE 10      /* "Wednesday\0" */

void clock_service_init(void);
clock_result_t clock_service_get_time(hal_time_t *out);
clock_result_t clock_service_set_time(cap_subject_id_t subject_id,
                                      const hal_time_t *new_time);

/**
 * Get approximate seconds since 2000-01-01 00:00:00 UTC.
 */
clock_result_t clock_service_get_epoch(uint32_t *out);

/**
 * Get monotonic tick count (increments on each clock read since boot).
 */
uint32_t clock_service_get_ticks(void);

void clock_service_format_datetime(const hal_time_t *t, char *buf,
                                   unsigned int buf_size);
void clock_service_format_time(const hal_time_t *t, char *buf,
                               unsigned int buf_size);
void clock_service_format_date(const hal_time_t *t, char *buf,
                               unsigned int buf_size);
void clock_service_format_day_name(const hal_time_t *t, char *buf,
                                   unsigned int buf_size);

#endif
