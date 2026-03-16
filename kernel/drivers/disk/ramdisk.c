/**
 * @file ramdisk.c
 * @brief In-memory RAM disk driver.
 *
 * Purpose:
 *   Simulates a block storage device entirely in RAM. Provides sector-level
 *   read and write operations on a statically-allocated memory buffer, enabling
 *   filesystem persistence testing without requiring physical disk hardware.
 *
 * Interactions:
 *   - storage_hal.c: registered as the default storage backend. The HAL
 *     delegates read/write calls to ramdisk_read_sector/ramdisk_write_sector.
 *   - fs_service.c: indirectly used for filesystem persistence (save/load)
 *     when the storage HAL routes through the ramdisk backend.
 *
 * Launched by:
 *   ramdisk_init() is called by storage_hal_init() during kernel boot.
 *   Not a standalone process; compiled into the kernel image.
 */

#include "ramdisk.h"

#include "../../hal/storage_hal.h"

enum {
  RAMDISK_BLOCK_SIZE = 512,
  RAMDISK_BLOCK_COUNT = 128,
};

static uint8_t ramdisk_blocks[RAMDISK_BLOCK_COUNT][RAMDISK_BLOCK_SIZE];

static storage_result_t ramdisk_read(uint32_t lba, uint8_t *buffer, size_t buffer_size) {
  size_t i = 0u;

  if (lba >= RAMDISK_BLOCK_COUNT) {
    return STORAGE_ERR_LBA_INVALID;
  }

  if (buffer == 0 || buffer_size < RAMDISK_BLOCK_SIZE) {
    return STORAGE_ERR_BUFFER_INVALID;
  }

  for (i = 0u; i < RAMDISK_BLOCK_SIZE; ++i) {
    buffer[i] = ramdisk_blocks[lba][i];
  }

  return STORAGE_OK;
}

static storage_result_t ramdisk_write(uint32_t lba, const uint8_t *buffer, size_t buffer_size) {
  size_t i = 0u;

  if (lba >= RAMDISK_BLOCK_COUNT) {
    return STORAGE_ERR_LBA_INVALID;
  }

  if (buffer == 0 || buffer_size < RAMDISK_BLOCK_SIZE) {
    return STORAGE_ERR_BUFFER_INVALID;
  }

  for (i = 0u; i < RAMDISK_BLOCK_SIZE; ++i) {
    ramdisk_blocks[lba][i] = buffer[i];
  }

  return STORAGE_OK;
}

void ramdisk_init(void) {
  size_t block = 0u;
  size_t offset = 0u;
  static const storage_device_t ramdisk_device = {
      STORAGE_BACKEND_RAMDISK,
      "ramdisk",
      RAMDISK_BLOCK_SIZE,
      RAMDISK_BLOCK_COUNT,
      ramdisk_read,
      ramdisk_write,
  };

  for (block = 0u; block < RAMDISK_BLOCK_COUNT; ++block) {
    for (offset = 0u; offset < RAMDISK_BLOCK_SIZE; ++offset) {
      ramdisk_blocks[block][offset] = 0u;
    }
  }

  storage_hal_register_primary(&ramdisk_device);
}
