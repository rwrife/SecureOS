/**
 * @file serial_hal.c
 * @brief Hardware Abstraction Layer for serial communication devices.
 *
 * Purpose:
 *   Stores the active serial backend and routes all serial reads/writes
 *   through a uniform interface. This isolates core kernel code from any
 *   specific serial hardware implementation.
 *
 * Interactions:
 *   - drivers/serial/pc_com.c: registers and initializes PC COM backend.
 *   - core/console.c: polls serial_hal_try_read_char() for shell input.
 *   - core/session_manager.c / core/kmain.c: emit logs via serial_hal_write().
 *
 * Launched by:
 *   serial_hal_register_primary() is called by serial drivers during boot.
 *   Not a standalone process; compiled into the kernel image.
 */

#include "serial_hal.h"

static const serial_device_t *serial_primary_device;
static int serial_initialized;

void serial_hal_reset_for_tests(void) {
  serial_primary_device = 0;
  serial_initialized = 0;
}

void serial_hal_register_primary(const serial_device_t *device) {
  serial_primary_device = device;
  serial_initialized = 0;
}

int serial_hal_init(void) {
  if (serial_primary_device == 0 || serial_primary_device->init == 0) {
    return 0;
  }

  serial_initialized = serial_primary_device->init() ? 1 : 0;
  return serial_initialized;
}

int serial_hal_ready(void) {
  return serial_initialized &&
         serial_primary_device != 0 &&
         serial_primary_device->try_read_char != 0 &&
         serial_primary_device->write_char != 0;
}

serial_backend_t serial_hal_backend(void) {
  if (!serial_hal_ready()) {
    return SERIAL_BACKEND_UNKNOWN;
  }
  return serial_primary_device->backend;
}

const char *serial_hal_backend_name(void) {
  if (!serial_hal_ready() || serial_primary_device->backend_name == 0) {
    return "unknown";
  }
  return serial_primary_device->backend_name;
}

int serial_hal_try_read_char(char *out_char) {
  if (out_char == 0) {
    return 0;
  }

  if (!serial_hal_ready()) {
    return 0;
  }

  return serial_primary_device->try_read_char(out_char);
}

void serial_hal_write_char(char value) {
  if (!serial_hal_ready()) {
    return;
  }

  serial_primary_device->write_char(value);
}

void serial_hal_write(const char *message) {
  if (message == 0 || !serial_hal_ready()) {
    return;
  }

  if (serial_primary_device->write != 0) {
    serial_primary_device->write(message);
    return;
  }

  while (*message != '\0') {
    serial_primary_device->write_char(*message++);
  }
}
