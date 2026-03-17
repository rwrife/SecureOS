/**
 * @file serial.c
 * @brief Legacy serial compatibility facade.
 *
 * Purpose:
 *   Preserves the historical serial_* API while delegating all behavior to
 *   the architecture-agnostic serial HAL and its currently selected backend.
 *   This allows existing call sites to compile unchanged while core code
 *   migrates to the HAL directly.
 *
 * Interactions:
 *   - drivers/serial/pc_com.c: provides the default x86 COM backend.
 *   - hal/serial_hal.c: provides backend registration and dispatch.
 *   - core subsystem code may still include this file during migration.
 *
 * Launched by:
 *   Called by boot and console paths as needed. Not a standalone process;
 *   compiled into the kernel image.
 */

#include "serial.h"

#include "../../drivers/serial/pc_com.h"
#include "../../hal/serial_hal.h"

int serial_try_read_char(char *out_char) {
  return serial_hal_try_read_char(out_char);
}

void serial_init(void) {
  (void)pc_com_serial_init_primary();
}

void serial_write(const char *s) {
  serial_hal_write(s);
}
