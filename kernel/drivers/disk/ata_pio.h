#ifndef SECUREOS_ATA_PIO_H
#define SECUREOS_ATA_PIO_H

#include <stdint.h>

int ata_pio_init_primary(void);
int ata_pio_is_active(void);
uint32_t ata_pio_block_count(void);

#endif
