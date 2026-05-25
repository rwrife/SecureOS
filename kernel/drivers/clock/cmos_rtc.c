/**
 * @file cmos_rtc.c
 * @brief x86 CMOS RTC hardware driver – reads date/time from MC146818 chip.
 *
 * Purpose:
 *   Reads the real-time clock present in all PC-compatible systems via I/O
 *   ports 0x70 (index) and 0x71 (data). Decodes BCD-encoded registers into
 *   binary values and registers itself with the clock HAL as the default
 *   clock backend.
 *
 * Interactions:
 *   - clock_hal.c: registers as the clock backend via clock_hal_register().
 *   - kmain.c: called during boot via cmos_rtc_init().
 *   - serial_hal.h: used for debug logging during initialization.
 *
 * Launched by:
 *   Not a standalone process. Called from kmain() during kernel boot.
 *   Compiled into the kernel image.
 */

#include "cmos_rtc.h"

#include "../../hal/clock_hal.h"
#include "../../hal/serial_hal.h"

enum {
  CMOS_INDEX_PORT = 0x70,
  CMOS_DATA_PORT = 0x71,
  CMOS_REG_SECONDS = 0x00,
  CMOS_REG_MINUTES = 0x02,
  CMOS_REG_HOURS = 0x04,
  CMOS_REG_WEEKDAY = 0x06,
  CMOS_REG_DAY = 0x07,
  CMOS_REG_MONTH = 0x08,
  CMOS_REG_YEAR = 0x09,
  CMOS_REG_CENTURY = 0x32,
  CMOS_REG_STATUS_A = 0x0A,
  CMOS_REG_STATUS_B = 0x0B,
  CMOS_STATUS_A_UIP = 0x80,
  CMOS_STATUS_B_24H = 0x02,
  CMOS_STATUS_B_BIN = 0x04,
};

static int g_cmos_rtc_active;
static int g_cmos_bcd_mode;
static int g_cmos_24h_mode;

#ifdef __INTELLISENSE__
static inline void outb(unsigned short port, unsigned char val) {
  (void)port;
  (void)val;
}
static inline unsigned char inb(unsigned short port) {
  (void)port;
  return 0u;
}
#else
static inline void outb(unsigned short port, unsigned char val) {
  __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline unsigned char inb(unsigned short port) {
  unsigned char ret;
  __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}
#endif

static unsigned char cmos_read_register(unsigned char reg) {
  outb(CMOS_INDEX_PORT, reg);
  return inb(CMOS_DATA_PORT);
}

static int cmos_is_updating(void) {
  return (cmos_read_register(CMOS_REG_STATUS_A) & CMOS_STATUS_A_UIP) != 0;
}

static unsigned char bcd_to_binary(unsigned char bcd) {
  return (unsigned char)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

static clock_result_t cmos_rtc_read_time(hal_time_t *out) {
  unsigned char sec, min, hour, day, month, year, century, weekday;
  int retries = 1000;

  if (out == 0) {
    return CLOCK_ERR_INVALID;
  }

  /* Wait for update-not-in-progress */
  while (cmos_is_updating() && retries > 0) {
    retries--;
  }

  sec = cmos_read_register(CMOS_REG_SECONDS);
  min = cmos_read_register(CMOS_REG_MINUTES);
  hour = cmos_read_register(CMOS_REG_HOURS);
  weekday = cmos_read_register(CMOS_REG_WEEKDAY);
  day = cmos_read_register(CMOS_REG_DAY);
  month = cmos_read_register(CMOS_REG_MONTH);
  year = cmos_read_register(CMOS_REG_YEAR);
  century = cmos_read_register(CMOS_REG_CENTURY);

  if (g_cmos_bcd_mode) {
    sec = bcd_to_binary(sec);
    min = bcd_to_binary(min);
    hour = bcd_to_binary(hour);
    day = bcd_to_binary(day);
    month = bcd_to_binary(month);
    year = bcd_to_binary(year);
    century = bcd_to_binary(century);
  }

  if (!g_cmos_24h_mode) {
    /* Convert 12-hour to 24-hour if needed */
    if ((hour & 0x80) != 0) {
      hour = (unsigned char)(((hour & 0x7F) % 12) + 12);
    } else {
      hour = (unsigned char)(hour % 12);
    }
  }

  out->year = (uint16_t)(century * 100 + year);
  out->month = month;
  out->day = day;
  out->hour = hour;
  out->minute = min;
  out->second = sec;
  out->weekday = weekday;

  return CLOCK_OK;
}

static clock_device_t g_cmos_device = {
    .backend = CLOCK_BACKEND_CMOS_RTC,
    .backend_name = "cmos-rtc",
    .read_time = cmos_rtc_read_time,
};

int cmos_rtc_init(void) {
  unsigned char status_b;

  status_b = cmos_read_register(CMOS_REG_STATUS_B);
  g_cmos_24h_mode = (status_b & CMOS_STATUS_B_24H) != 0;
  g_cmos_bcd_mode = (status_b & CMOS_STATUS_B_BIN) == 0;

  if (clock_hal_register(&g_cmos_device) != CLOCK_OK) {
    serial_hal_write("[cmos_rtc] failed to register with clock HAL\n");
    g_cmos_rtc_active = 0;
    return 0;
  }

  g_cmos_rtc_active = 1;
  serial_hal_write("[cmos_rtc] initialized\n");
  return 1;
}

int cmos_rtc_is_available(void) {
  return g_cmos_rtc_active;
}
