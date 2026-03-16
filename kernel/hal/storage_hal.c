/**
 * @file storage_hal.c
 * @brief Hardware Abstraction Layer for block storage devices.
 *
 * Purpose:
 *   Provides a uniform sector-level read/write interface that decouples
 *   the filesystem from concrete storage drivers.  The active backend
 *   (ramdisk or ATA PIO) is registered at init time and all higher-
 *   level I/O is routed through this abstraction.
 *
 * Interactions:
 *   - ramdisk.c / ata_pio.c: concrete drivers register themselves as
 *     the primary storage device via storage_hal_init.
 *   - fs_service.c: all filesystem sector reads and writes call
 *     storage_hal_read / storage_hal_write.
 *   - console.c: the "storage" shell command queries HAL status.
 *
 * Launched by:
 *   storage_hal_init() is called from kmain() during kernel boot.
 *   Not a standalone process; compiled into the kernel image.
 */

#include "storage_hal.h"

static const storage_device_t *storage_primary_device;

void storage_hal_reset_for_tests(void) {
  storage_primary_device = 0;
}

void storage_hal_register_primary(const storage_device_t *device) {
  storage_primary_device = device;
}

int storage_hal_ready(void) {
  return storage_primary_device != 0 && storage_primary_device->read_block != 0 &&
         storage_primary_device->write_block != 0;
}

storage_backend_t storage_hal_backend(void) {
  if (!storage_hal_ready()) {
    return STORAGE_BACKEND_UNKNOWN;
  }

  return storage_primary_device->backend;
}

const char *storage_hal_backend_name(void) {
  if (!storage_hal_ready() || storage_primary_device->backend_name == 0) {
    return "unknown";
  }

  return storage_primary_device->backend_name;
}

uint32_t storage_hal_block_size(void) {
  if (!storage_hal_ready()) {
    return 0u;
  }

  return storage_primary_device->block_size;
}

uint32_t storage_hal_block_count(void) {
  if (!storage_hal_ready()) {
    return 0u;
  }

  return storage_primary_device->block_count;
}

storage_result_t storage_hal_read(uint32_t lba, uint8_t *buffer, size_t buffer_size) {
  if (buffer == 0) {
    return STORAGE_ERR_BUFFER_INVALID;
  }

  if (!storage_hal_ready()) {
    return STORAGE_ERR_NOT_READY;
  }

  if (lba >= storage_primary_device->block_count) {
    return STORAGE_ERR_LBA_INVALID;
  }

  if (buffer_size < storage_primary_device->block_size) {
    return STORAGE_ERR_BUFFER_INVALID;
  }

  return storage_primary_device->read_block(lba, buffer, buffer_size);
}

storage_result_t storage_hal_write(uint32_t lba, const uint8_t *buffer, size_t buffer_size) {
  if (buffer == 0) {
    return STORAGE_ERR_BUFFER_INVALID;
  }

  if (!storage_hal_ready()) {
    return STORAGE_ERR_NOT_READY;
  }

  if (lba >= storage_primary_device->block_count) {
    return STORAGE_ERR_LBA_INVALID;
  }

  if (buffer_size < storage_primary_device->block_size) {
    return STORAGE_ERR_BUFFER_INVALID;
  }

  return storage_primary_device->write_block(lba, buffer, buffer_size);
}
