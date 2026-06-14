#ifndef IDE_H
#define IDE_H

#include "stdint.h"

#define IDE_PRIMARY_BASE  0x1F0
#define IDE_PRIMARY_CTRL  0x3F6
#define IDE_SECONDARY_BASE 0x170
#define IDE_SECONDARY_CTRL 0x376
#define IDE_SECTOR_SIZE   512
#define IDE_MAX_DRIVES    4

typedef struct {
    uint16_t base_port;
    uint16_t ctrl_port;
    uint8_t type;
    uint16_t cylinders;
    uint16_t heads;
    uint16_t sectors;
    uint32_t size_sectors;
    uint8_t model[41];
    uint8_t master;
} ide_device_t;

void ide_init(void);
int ide_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buf);
int ide_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void *buf);
int ide_identify(uint8_t drive, ide_device_t *dev);

#endif
