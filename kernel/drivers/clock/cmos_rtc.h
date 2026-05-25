#ifndef SECUREOS_CMOS_RTC_H
#define SECUREOS_CMOS_RTC_H

#include <stdint.h>

/**
 * Initialize the CMOS RTC driver and register with the clock HAL.
 * Returns 1 on success, 0 if RTC is not available.
 */
int cmos_rtc_init(void);

/**
 * Returns 1 if the CMOS RTC was successfully initialized.
 */
int cmos_rtc_is_available(void);

#endif
