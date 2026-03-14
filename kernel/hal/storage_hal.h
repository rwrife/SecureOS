#ifndef SECUREOS_STORAGE_HAL_H
#define SECUREOS_STORAGE_HAL_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
  STORAGE_OK = 0,
  STORAGE_ERR_NOT_READY = 1,
  STORAGE_ERR_LBA_INVALID = 2,
  STORAGE_ERR_BUFFER_INVALID = 3,
} storage_result_t;

typedef enum {
  STORAGE_BACKEND_UNKNOWN = 0,
  STORAGE_BACKEND_RAMDISK = 1,
  STORAGE_BACKEND_ATA_PIO = 2,
} storage_backend_t;

typedef storage_result_t (*storage_read_fn_t)(uint32_t lba, uint8_t *buffer, size_t buffer_size);
typedef storage_result_t (*storage_write_fn_t)(uint32_t lba, const uint8_t *buffer, size_t buffer_size);

typedef struct {
  storage_backend_t backend;
  const char *backend_name;
  uint32_t block_size;
  uint32_t block_count;
  storage_read_fn_t read_block;
  storage_write_fn_t write_block;
} storage_device_t;

void storage_hal_reset_for_tests(void);
void storage_hal_register_primary(const storage_device_t *device);
int storage_hal_ready(void);
storage_backend_t storage_hal_backend(void);
const char *storage_hal_backend_name(void);
uint32_t storage_hal_block_size(void);
uint32_t storage_hal_block_count(void);
storage_result_t storage_hal_read(uint32_t lba, uint8_t *buffer, size_t buffer_size);
storage_result_t storage_hal_write(uint32_t lba, const uint8_t *buffer, size_t buffer_size);

#endif
