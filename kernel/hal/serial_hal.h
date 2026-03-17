#ifndef SECUREOS_SERIAL_HAL_H
#define SECUREOS_SERIAL_HAL_H

/**
 * @file serial_hal.h
 * @brief Hardware Abstraction Layer for serial communication devices.
 *
 * Purpose:
 *   Defines a backend-agnostic serial interface so kernel services can use
 *   serial I/O without depending on architecture-specific implementations.
 *   Concrete drivers (PC COM, GPIO bit-bang UART, etc.) register a primary
 *   serial device at boot.
 *
 * Interactions:
 *   - drivers/serial/pc_com.c: registers the standard x86 COM backend.
 *   - core/console.c: reads console input from serial_hal_try_read_char().
 *   - core/session_manager.c and core/kmain.c: write boot and status logs
 *     through serial_hal_write().
 *
 * Launched by:
 *   A concrete serial driver calls serial_hal_register_primary() during
 *   boot initialization. Not a standalone process; compiled into kernel.
 */

typedef enum {
  SERIAL_OK = 0,
  SERIAL_ERR_NOT_READY = 1,
  SERIAL_ERR_BUFFER_INVALID = 2,
  SERIAL_ERR_RX_EMPTY = 3,
} serial_result_t;

typedef enum {
  SERIAL_BACKEND_UNKNOWN = 0,
  SERIAL_BACKEND_PC_COM = 1,
  SERIAL_BACKEND_GPIO = 2,
} serial_backend_t;

typedef int (*serial_init_fn_t)(void);
typedef int (*serial_try_read_char_fn_t)(char *out_char);
typedef void (*serial_write_char_fn_t)(char value);
typedef void (*serial_write_fn_t)(const char *message);

typedef struct {
  serial_backend_t backend;
  const char *backend_name;
  serial_init_fn_t init;
  serial_try_read_char_fn_t try_read_char;
  serial_write_char_fn_t write_char;
  serial_write_fn_t write;
} serial_device_t;

void serial_hal_reset_for_tests(void);
void serial_hal_register_primary(const serial_device_t *device);
int serial_hal_init(void);
int serial_hal_ready(void);
serial_backend_t serial_hal_backend(void);
const char *serial_hal_backend_name(void);
int serial_hal_try_read_char(char *out_char);
void serial_hal_write_char(char value);
void serial_hal_write(const char *message);

#endif
