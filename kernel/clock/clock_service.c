/**
 * @file clock_service.c
 * @brief Kernel clock service – formatted time read, offset-based set, cap-gated.
 *
 * Purpose:
 *   Sits above the clock HAL and provides high-level time operations:
 *   reading the current time (with software offset applied), setting time
 *   via a capability-gated offset mechanism, epoch conversion, tick counting,
 *   and formatted string output.
 *
 * Interactions:
 *   - clock_hal.c: reads hardware time via clock_hal_read_time().
 *   - cap_table.c: checks CAP_CLOCK_SET before allowing time modification.
 *   - launcher_exec.c: syscall handlers call into this service.
 *   - serial_hal.h: used for debug logging.
 *
 * Launched by:
 *   Initialized from kmain() via clock_service_init(). Not a standalone
 *   process; compiled into the kernel image.
 */

#include "clock_service.h"

#include "../cap/cap_table.h"
#include "../hal/serial_hal.h"

/* Software offset in seconds applied to hardware time */
static int32_t g_clock_offset_seconds;

/* Monotonic tick counter, incremented on each time read */
static uint32_t g_tick_count;

/* Days in each month for non-leap years */
static const uint16_t days_in_month[] = {
    0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static int is_leap_year(uint16_t year) {
  if ((year % 4) != 0) return 0;
  if ((year % 100) != 0) return 1;
  if ((year % 400) != 0) return 0;
  return 1;
}

static uint16_t days_in_month_for(uint8_t month, uint16_t year) {
  if (month == 2 && is_leap_year(year)) return 29;
  if (month < 1 || month > 12) return 30;
  return days_in_month[month];
}

/**
 * Approximate seconds since 2000-01-01 00:00:00. Not a full POSIX epoch —
 * just enough for offset deltas and epoch reporting.
 */
static int32_t time_to_epoch_approx(const hal_time_t *t) {
  int32_t days = 0;
  uint16_t y;
  uint8_t m;

  for (y = 2000; y < t->year; y++) {
    days += is_leap_year(y) ? 366 : 365;
  }
  for (m = 1; m < t->month && m <= 12; m++) {
    days += (int32_t)days_in_month_for(m, t->year);
  }
  days += (int32_t)(t->day - 1);

  return days * 86400 + (int32_t)t->hour * 3600 + (int32_t)t->minute * 60 +
         (int32_t)t->second;
}

static void epoch_approx_to_time(int32_t epoch, hal_time_t *out) {
  int32_t remaining = epoch;
  uint16_t year = 2000;
  uint8_t month = 1;
  int32_t year_days;
  uint16_t mdays;

  if (remaining < 0) {
    remaining = 0;
  }

  for (;;) {
    year_days = is_leap_year(year) ? 366 : 365;
    if (remaining < year_days * 86400) break;
    remaining -= year_days * 86400;
    year++;
    if (year > 2199) break;
  }

  for (month = 1; month <= 12; month++) {
    mdays = days_in_month_for(month, year);
    if (remaining < (int32_t)mdays * 86400) break;
    remaining -= (int32_t)mdays * 86400;
  }
  if (month > 12) month = 12;

  out->year = year;
  out->month = month;
  out->day = (uint8_t)(remaining / 86400 + 1);
  remaining %= 86400;
  out->hour = (uint8_t)(remaining / 3600);
  remaining %= 3600;
  out->minute = (uint8_t)(remaining / 60);
  out->second = (uint8_t)(remaining % 60);
  out->weekday = 0; /* weekday not computed from epoch */
}

void clock_service_init(void) {
  g_clock_offset_seconds = 0;
  g_tick_count = 0;
  serial_hal_write("[clock_service] initialized\n");
}

clock_result_t clock_service_get_time(hal_time_t *out) {
  hal_time_t hw_time;
  clock_result_t result;
  int32_t epoch;

  if (out == 0) {
    return CLOCK_ERR_INVALID;
  }

  result = clock_hal_read_time(&hw_time);
  if (result != CLOCK_OK) {
    return result;
  }

  g_tick_count++;

  if (g_clock_offset_seconds == 0) {
    *out = hw_time;
    return CLOCK_OK;
  }

  /* Apply software offset */
  epoch = time_to_epoch_approx(&hw_time) + g_clock_offset_seconds;
  epoch_approx_to_time(epoch, out);
  out->weekday = hw_time.weekday; /* preserve hardware weekday */

  return CLOCK_OK;
}

clock_result_t clock_service_set_time(cap_subject_id_t subject_id,
                                      const hal_time_t *new_time) {
  hal_time_t hw_time;
  clock_result_t result;
  int32_t hw_epoch, new_epoch;

  if (new_time == 0) {
    return CLOCK_ERR_INVALID;
  }

  if (cap_table_check(subject_id, CAP_CLOCK_SET) != CAP_OK) {
    return CLOCK_ERR_DENIED;
  }

  result = clock_hal_read_time(&hw_time);
  if (result != CLOCK_OK) {
    return result;
  }

  hw_epoch = time_to_epoch_approx(&hw_time);
  new_epoch = time_to_epoch_approx(new_time);
  g_clock_offset_seconds = new_epoch - hw_epoch;

  return CLOCK_OK;
}

clock_result_t clock_service_get_epoch(uint32_t *out) {
  hal_time_t t;
  clock_result_t result;

  if (out == 0) {
    return CLOCK_ERR_INVALID;
  }

  result = clock_service_get_time(&t);
  if (result != CLOCK_OK) {
    return result;
  }

  *out = (uint32_t)time_to_epoch_approx(&t);
  return CLOCK_OK;
}

uint32_t clock_service_get_ticks(void) {
  return g_tick_count;
}

/* Helper: write a zero-padded decimal number into buf */
static void write_decimal(char *buf, unsigned int val, int width) {
  int i;
  for (i = width - 1; i >= 0; i--) {
    buf[i] = (char)('0' + (val % 10));
    val /= 10;
  }
}

void clock_service_format_datetime(const hal_time_t *t, char *buf,
                                   unsigned int buf_size) {
  if (buf == 0 || buf_size < CLOCK_FMT_DATETIME_SIZE || t == 0) {
    if (buf != 0 && buf_size > 0) buf[0] = '\0';
    return;
  }

  write_decimal(buf, t->year, 4);
  buf[4] = '-';
  write_decimal(buf + 5, t->month, 2);
  buf[7] = '-';
  write_decimal(buf + 8, t->day, 2);
  buf[10] = ' ';
  write_decimal(buf + 11, t->hour, 2);
  buf[13] = ':';
  write_decimal(buf + 14, t->minute, 2);
  buf[16] = ':';
  write_decimal(buf + 17, t->second, 2);
  buf[19] = '\0';
}

void clock_service_format_time(const hal_time_t *t, char *buf,
                               unsigned int buf_size) {
  if (buf == 0 || buf_size < CLOCK_FMT_TIME_SIZE || t == 0) {
    if (buf != 0 && buf_size > 0) buf[0] = '\0';
    return;
  }

  write_decimal(buf, t->hour, 2);
  buf[2] = ':';
  write_decimal(buf + 3, t->minute, 2);
  buf[5] = ':';
  write_decimal(buf + 6, t->second, 2);
  buf[8] = '\0';
}

void clock_service_format_date(const hal_time_t *t, char *buf,
                               unsigned int buf_size) {
  if (buf == 0 || buf_size < CLOCK_FMT_DATE_SIZE || t == 0) {
    if (buf != 0 && buf_size > 0) buf[0] = '\0';
    return;
  }

  write_decimal(buf, t->year, 4);
  buf[4] = '-';
  write_decimal(buf + 5, t->month, 2);
  buf[7] = '-';
  write_decimal(buf + 8, t->day, 2);
  buf[10] = '\0';
}

void clock_service_format_day_name(const hal_time_t *t, char *buf,
                                   unsigned int buf_size) {
  static const char *day_names[] = {"", "Sunday",    "Monday",   "Tuesday",
                                    "Wednesday", "Thursday", "Friday",
                                    "Saturday"};
  const char *name;
  unsigned int i = 0;

  if (buf == 0 || buf_size == 0 || t == 0) {
    if (buf != 0 && buf_size > 0) buf[0] = '\0';
    return;
  }

  if (t->weekday >= 1 && t->weekday <= 7) {
    name = day_names[t->weekday];
  } else {
    name = "Unknown";
  }

  while (name[i] != '\0' && i + 1 < buf_size) {
    buf[i] = name[i];
    i++;
  }
  buf[i] = '\0';
}
