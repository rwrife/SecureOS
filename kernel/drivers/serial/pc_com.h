#ifndef SECUREOS_PC_COM_H
#define SECUREOS_PC_COM_H

/**
 * @file pc_com.h
 * @brief Standard PC COM serial driver registration helpers.
 *
 * Purpose:
 *   Declares initialization entry points for the standard PC COM serial
 *   backend. The driver configures a classic UART-compatible COM port and
 *   registers it with serial HAL as the active serial backend.
 *
 * Interactions:
 *   - serial_hal.c: pc_com driver registers a serial_device_t implementation.
 *   - kmain.c: calls pc_com_serial_init_primary() during early boot.
 *
 * Launched by:
 *   Called from kernel boot initialization, not a standalone process.
 */

int pc_com_serial_init_primary(void);
int pc_com_serial_init_primary_at(unsigned short io_base);

#endif
