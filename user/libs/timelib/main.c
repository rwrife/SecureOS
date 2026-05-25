/**
 * @file main.c
 * @brief timelib – shared library for date/time operations.
 *
 * Purpose:
 *   Provides user-space applications with time query and formatting
 *   functions. Wraps the os_clock_* syscalls and adds formatting logic
 *   so applications can easily display date/time information.
 *
 * Interactions:
 *   - lib/timelib.h: public API header used by apps that load this library.
 *   - secureos_api.h: calls os_clock_get, os_clock_epoch, os_clock_ticks
 *     syscalls to retrieve time from the kernel.
 *   - process.c (launcher_exec.c): the library ELF is loaded and registered
 *     by the process subsystem loadlib path.
 *
 * Launched by:
 *   Loaded as a shared library via "loadlib timelib" at the console.
 *   Built as a standalone ELF binary placed in /lib/.
 */

#include "lib/timelib.h"
#include "secureos_api.h"

/* Helper: write a zero-padded decimal number into buf */
static void write_decimal(char *buf, unsigned int val, int width) {
  int i;
  for (i = width - 1; i >= 0; i--) {
    buf[i] = (char)('0' + (val % 10));
    val /= 10;
  }
}

timelib_status_t timelib_now(timelib_datetime_t *out) {
  char buf[20];
  os_status_t status;
  unsigned int val;
  int i;

  if (out == 0) {
    return TIMELIB_STATUS_ERROR;
  }

  status = os_clock_get(buf, sizeof(buf));
  if (status != OS_STATUS_OK) {
    return TIMELIB_STATUS_NOT_READY;
  }

  /* Parse "YYYY-MM-DD HH:MM:SS" */
  i = 0;
  val = 0;
  while (buf[i] >= '0' && buf[i] <= '9') { val = val * 10 + (unsigned int)(buf[i] - '0'); i++; }
  out->year = (uint16_t)val;
  if (buf[i] == '-') i++;

  val = 0;
  while (buf[i] >= '0' && buf[i] <= '9') { val = val * 10 + (unsigned int)(buf[i] - '0'); i++; }
  out->month = (uint8_t)val;
  if (buf[i] == '-') i++;

  val = 0;
  while (buf[i] >= '0' && buf[i] <= '9') { val = val * 10 + (unsigned int)(buf[i] - '0'); i++; }
  out->day = (uint8_t)val;
  if (buf[i] == ' ') i++;

  val = 0;
  while (buf[i] >= '0' && buf[i] <= '9') { val = val * 10 + (unsigned int)(buf[i] - '0'); i++; }
  out->hour = (uint8_t)val;
  if (buf[i] == ':') i++;

  val = 0;
  while (buf[i] >= '0' && buf[i] <= '9') { val = val * 10 + (unsigned int)(buf[i] - '0'); i++; }
  out->minute = (uint8_t)val;
  if (buf[i] == ':') i++;

  val = 0;
  while (buf[i] >= '0' && buf[i] <= '9') { val = val * 10 + (unsigned int)(buf[i] - '0'); i++; }
  out->second = (uint8_t)val;

  out->weekday = 0; /* weekday not provided by os_clock_get */
  return TIMELIB_STATUS_OK;
}

timelib_status_t timelib_epoch(unsigned int *out) {
  if (out == 0) {
    return TIMELIB_STATUS_ERROR;
  }
  if (os_clock_epoch(out) != OS_STATUS_OK) {
    return TIMELIB_STATUS_NOT_READY;
  }
  return TIMELIB_STATUS_OK;
}

timelib_status_t timelib_ticks(unsigned int *out) {
  if (out == 0) {
    return TIMELIB_STATUS_ERROR;
  }
  if (os_clock_ticks(out) != OS_STATUS_OK) {
    return TIMELIB_STATUS_NOT_READY;
  }
  return TIMELIB_STATUS_OK;
}

timelib_status_t timelib_format(const timelib_datetime_t *t, char *buf,
                                unsigned int buf_size) {
  if (t == 0 || buf == 0 || buf_size < 20) {
    return TIMELIB_STATUS_ERROR;
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
  return TIMELIB_STATUS_OK;
}

timelib_status_t timelib_format_time(const timelib_datetime_t *t, char *buf,
                                     unsigned int buf_size) {
  if (t == 0 || buf == 0 || buf_size < 9) {
    return TIMELIB_STATUS_ERROR;
  }
  write_decimal(buf, t->hour, 2);
  buf[2] = ':';
  write_decimal(buf + 3, t->minute, 2);
  buf[5] = ':';
  write_decimal(buf + 6, t->second, 2);
  buf[8] = '\0';
  return TIMELIB_STATUS_OK;
}

timelib_status_t timelib_format_date(const timelib_datetime_t *t, char *buf,
                                     unsigned int buf_size) {
  if (t == 0 || buf == 0 || buf_size < 11) {
    return TIMELIB_STATUS_ERROR;
  }
  write_decimal(buf, t->year, 4);
  buf[4] = '-';
  write_decimal(buf + 5, t->month, 2);
  buf[7] = '-';
  write_decimal(buf + 8, t->day, 2);
  buf[10] = '\0';
  return TIMELIB_STATUS_OK;
}

timelib_status_t timelib_day_name(uint8_t weekday, char *buf,
                                  unsigned int buf_size) {
  static const char *day_names[] = {"", "Sunday",    "Monday",   "Tuesday",
                                    "Wednesday", "Thursday", "Friday",
                                    "Saturday"};
  const char *name;
  unsigned int i = 0;

  if (buf == 0 || buf_size == 0) {
    return TIMELIB_STATUS_ERROR;
  }

  if (weekday >= 1 && weekday <= 7) {
    name = day_names[weekday];
  } else {
    name = "Unknown";
  }

  while (name[i] != '\0' && i + 1 < buf_size) {
    buf[i] = name[i];
    i++;
  }
  buf[i] = '\0';
  return TIMELIB_STATUS_OK;
}

int main(void) {
  return 0;
}
