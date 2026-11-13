#ifndef EDUOS_ATA_H
#define EDUOS_ATA_H
#include "common.h"

#define ATA_SECTOR_SIZE 512u

int      ata_init(void);                         /* E_OK or E_NODEV/E_IO */
bool     ata_present(void);
uint32_t ata_sector_count(void);
int      ata_read(uint32_t lba, uint32_t count, void *buf);
/* Writes below EDUFS_START_LBA are refused (they would overwrite boot code / kernel). */
int      ata_write(uint32_t lba, uint32_t count, const void *buf);
void     ata_print_info(void);

#endif
