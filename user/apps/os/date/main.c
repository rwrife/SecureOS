/**
 * @file main.c
 * @brief "date" OS command – displays or sets the system date and time.
 *
 * Purpose:
 *   User-space application that reads the current system clock and prints
 *   a formatted date/time string. Supports setting the time with the "set"
 *   subcommand, querying epoch seconds with --epoch, and querying ticks
 *   with --ticks.
 *
 * Interactions:
 *   - secureos_api.h: calls os_clock_get, os_clock_set, os_clock_epoch,
 *     os_clock_ticks syscalls.
 *   - os_console_write: outputs results to the console.
 *
 * Launched by:
 *   Invoked as a user-space application from the console via the "date"
 *   command. Built as a standalone binary placed in /os/.
 */

#include "secureos_api.h"

int main(void) {
  char args[128];
  char buf[64];
  os_status_t status;

  status = os_get_args(args, sizeof(args));
  if (status != OS_STATUS_OK) {
    args[0] = '\0';
  }

  /* date --epoch */
  if (args[0] == '-' && args[1] == '-' && args[2] == 'e') {
    unsigned int epoch = 0;
    status = os_clock_epoch(&epoch);
    if (status != OS_STATUS_OK) {
      os_console_write("clock not ready\n");
      return 1;
    }
    /* Convert epoch to string */
    {
      char num[12];
      int i = 0;
      unsigned int val = epoch;
      if (val == 0) {
        num[i++] = '0';
      } else {
        char tmp[12];
        int j = 0;
        while (val > 0) { tmp[j++] = (char)('0' + (val % 10)); val /= 10; }
        while (j > 0) { num[i++] = tmp[--j]; }
      }
      num[i++] = '\n';
      num[i] = '\0';
      os_console_write(num);
    }
    return 0;
  }

  /* date --ticks */
  if (args[0] == '-' && args[1] == '-' && args[2] == 't') {
    unsigned int ticks = 0;
    status = os_clock_ticks(&ticks);
    if (status != OS_STATUS_OK) {
      os_console_write("clock not ready\n");
      return 1;
    }
    {
      char num[12];
      int i = 0;
      unsigned int val = ticks;
      if (val == 0) {
        num[i++] = '0';
      } else {
        char tmp[12];
        int j = 0;
        while (val > 0) { tmp[j++] = (char)('0' + (val % 10)); val /= 10; }
        while (j > 0) { num[i++] = tmp[--j]; }
      }
      num[i++] = '\n';
      num[i] = '\0';
      os_console_write(num);
    }
    return 0;
  }

  /* date set YYYY-MM-DD HH:MM:SS */
  if (args[0] == 's' && args[1] == 'e' && args[2] == 't' && args[3] == ' ') {
    status = os_clock_set(args + 4);
    if (status == OS_STATUS_DENIED) {
      os_console_write("denied: missing CAP_CLOCK_SET\n");
      return 1;
    }
    if (status != OS_STATUS_OK) {
      os_console_write("failed to set time\n");
      return 1;
    }
    os_console_write("time updated\n");
    return 0;
  }

  /* Default: show current date/time */
  status = os_clock_get(buf, sizeof(buf));
  if (status != OS_STATUS_OK) {
    os_console_write("clock not ready\n");
    return 1;
  }
  /* Append newline */
  {
    int i = 0;
    while (buf[i] != '\0' && i < 62) i++;
    buf[i] = '\n';
    buf[i + 1] = '\0';
  }
  os_console_write(buf);
  return 0;
}
