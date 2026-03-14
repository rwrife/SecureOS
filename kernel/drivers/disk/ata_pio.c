#include "ata_pio.h"

#include "../../hal/storage_hal.h"

enum {
  ATA_PRIMARY_IO = 0x1F0,
  ATA_PRIMARY_CONTROL = 0x3F6,
  ATA_REG_DATA = 0x00,
  ATA_REG_ERROR = 0x01,
  ATA_REG_SECTOR_COUNT = 0x02,
  ATA_REG_LBA_LOW = 0x03,
  ATA_REG_LBA_MID = 0x04,
  ATA_REG_LBA_HIGH = 0x05,
  ATA_REG_DRIVE = 0x06,
  ATA_REG_STATUS = 0x07,
  ATA_REG_COMMAND = 0x07,
  ATA_CMD_READ_SECTORS = 0x20,
  ATA_CMD_WRITE_SECTORS = 0x30,
  ATA_CMD_IDENTIFY = 0xEC,
  ATA_STATUS_ERR = 0x01,
  ATA_STATUS_DRQ = 0x08,
  ATA_STATUS_DF = 0x20,
  ATA_STATUS_DRDY = 0x40,
  ATA_STATUS_BSY = 0x80,
  ATA_DRIVE_MASTER = 0xE0,
  ATA_BLOCK_SIZE = 512,
  ATA_DEFAULT_BLOCK_COUNT = 4096,
  ATA_IDENTIFY_MAX_LBA28_WORD = 60,
};

static uint32_t g_ata_block_count;
static int g_ata_active;
static storage_device_t g_ata_device;

#ifdef __INTELLISENSE__
static inline void outb(unsigned short port, unsigned char val) {
  (void)port;
  (void)val;
}

static inline unsigned char inb(unsigned short port) {
  (void)port;
  return 0u;
}

static inline void outw(unsigned short port, unsigned short val) {
  (void)port;
  (void)val;
}

static inline unsigned short inw(unsigned short port) {
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

static inline void outw(unsigned short port, unsigned short val) {
  __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned short inw(unsigned short port) {
  unsigned short ret;
  __asm__ __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}
#endif

static void ata_select_drive(void) {
  outb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_DRIVE), ATA_DRIVE_MASTER);
}

static void ata_io_wait(void) {
  inb((unsigned short)(ATA_PRIMARY_CONTROL));
  inb((unsigned short)(ATA_PRIMARY_CONTROL));
  inb((unsigned short)(ATA_PRIMARY_CONTROL));
  inb((unsigned short)(ATA_PRIMARY_CONTROL));
}

static int ata_wait_not_busy(void) {
  uint32_t spin = 0u;

  for (spin = 0u; spin < 100000u; ++spin) {
    if ((inb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_STATUS)) & ATA_STATUS_BSY) == 0u) {
      return 1;
    }
  }

  return 0;
}

static int ata_wait_drq(void) {
  uint32_t spin = 0u;
  unsigned char status = 0u;

  for (spin = 0u; spin < 100000u; ++spin) {
    status = inb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_STATUS));
    if ((status & ATA_STATUS_ERR) != 0u || (status & ATA_STATUS_DF) != 0u) {
      return 0;
    }
    if ((status & ATA_STATUS_BSY) == 0u && (status & ATA_STATUS_DRQ) != 0u) {
      return 1;
    }
  }

  return 0;
}

static storage_result_t ata_issue_lba28(uint32_t lba, unsigned char sector_count, unsigned char command) {
  if (lba >= 0x10000000u) {
    return STORAGE_ERR_LBA_INVALID;
  }

  if (!ata_wait_not_busy()) {
    return STORAGE_ERR_NOT_READY;
  }

  ata_select_drive();
  ata_io_wait();

  outb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_SECTOR_COUNT), sector_count);
  outb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_LBA_LOW), (unsigned char)(lba & 0xFFu));
  outb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_LBA_MID), (unsigned char)((lba >> 8u) & 0xFFu));
  outb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH), (unsigned char)((lba >> 16u) & 0xFFu));
  outb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_DRIVE),
       (unsigned char)(ATA_DRIVE_MASTER | ((lba >> 24u) & 0x0Fu)));
  outb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_COMMAND), command);

  return STORAGE_OK;
}

static storage_result_t ata_read(uint32_t lba, uint8_t *buffer, size_t buffer_size) {
  size_t index = 0u;

  if (!g_ata_active) {
    return STORAGE_ERR_NOT_READY;
  }
  if (buffer == 0 || buffer_size < ATA_BLOCK_SIZE) {
    return STORAGE_ERR_BUFFER_INVALID;
  }
  if (lba >= g_ata_block_count) {
    return STORAGE_ERR_LBA_INVALID;
  }
  if (ata_issue_lba28(lba, 1u, ATA_CMD_READ_SECTORS) != STORAGE_OK) {
    return STORAGE_ERR_NOT_READY;
  }
  if (!ata_wait_drq()) {
    return STORAGE_ERR_NOT_READY;
  }

  for (index = 0u; index < ATA_BLOCK_SIZE; index += 2u) {
    unsigned short word = inw((unsigned short)(ATA_PRIMARY_IO + ATA_REG_DATA));
    buffer[index] = (uint8_t)(word & 0xFFu);
    buffer[index + 1u] = (uint8_t)((word >> 8u) & 0xFFu);
  }

  ata_io_wait();
  return STORAGE_OK;
}

static storage_result_t ata_write(uint32_t lba, const uint8_t *buffer, size_t buffer_size) {
  size_t index = 0u;

  if (!g_ata_active) {
    return STORAGE_ERR_NOT_READY;
  }
  if (buffer == 0 || buffer_size < ATA_BLOCK_SIZE) {
    return STORAGE_ERR_BUFFER_INVALID;
  }
  if (lba >= g_ata_block_count) {
    return STORAGE_ERR_LBA_INVALID;
  }
  if (ata_issue_lba28(lba, 1u, ATA_CMD_WRITE_SECTORS) != STORAGE_OK) {
    return STORAGE_ERR_NOT_READY;
  }
  if (!ata_wait_drq()) {
    return STORAGE_ERR_NOT_READY;
  }

  for (index = 0u; index < ATA_BLOCK_SIZE; index += 2u) {
    unsigned short word = (unsigned short)buffer[index] | (unsigned short)((unsigned short)buffer[index + 1u] << 8u);
    outw((unsigned short)(ATA_PRIMARY_IO + ATA_REG_DATA), word);
  }

  ata_io_wait();
  return ata_wait_not_busy() ? STORAGE_OK : STORAGE_ERR_NOT_READY;
}

int ata_pio_init_primary(void) {
  uint16_t identify[256];
  uint32_t index = 0u;
  uint32_t lba28_sectors = 0u;

  g_ata_active = 0;
  g_ata_block_count = 0u;

  ata_select_drive();
  ata_io_wait();
  outb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_SECTOR_COUNT), 0u);
  outb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_LBA_LOW), 0u);
  outb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_LBA_MID), 0u);
  outb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH), 0u);
  outb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);

  if (inb((unsigned short)(ATA_PRIMARY_IO + ATA_REG_STATUS)) == 0u) {
    return 0;
  }
  if (!ata_wait_drq()) {
    return 0;
  }

  for (index = 0u; index < 256u; ++index) {
    identify[index] = inw((unsigned short)(ATA_PRIMARY_IO + ATA_REG_DATA));
  }

  lba28_sectors = (uint32_t)identify[ATA_IDENTIFY_MAX_LBA28_WORD] |
                  ((uint32_t)identify[ATA_IDENTIFY_MAX_LBA28_WORD + 1u] << 16u);
  if (lba28_sectors == 0u) {
    lba28_sectors = ATA_DEFAULT_BLOCK_COUNT;
  }

  g_ata_block_count = lba28_sectors;
  g_ata_device.block_size = ATA_BLOCK_SIZE;
  g_ata_device.block_count = g_ata_block_count;
  g_ata_device.backend = STORAGE_BACKEND_ATA_PIO;
  g_ata_device.backend_name = "ata-pio";
  g_ata_device.read_block = ata_read;
  g_ata_device.write_block = ata_write;
  storage_hal_register_primary(&g_ata_device);
  g_ata_active = 1;
  return 1;
}

int ata_pio_is_active(void) {
  return g_ata_active;
}

uint32_t ata_pio_block_count(void) {
  return g_ata_block_count;
}
