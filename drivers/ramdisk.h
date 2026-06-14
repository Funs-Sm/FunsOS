#ifndef RAMDISK_H
#define RAMDISK_H

#include "stdint.h"

#define RAMDISK_SECTOR_SIZE 512
#define RAMDISK_MAX_SIZE    (4*1024*1024)

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t sector_count;
    uint32_t ref_count;
} ramdisk_t;

int ramdisk_init(uint32_t size);
ramdisk_t *ramdisk_create(uint32_t size);
int ramdisk_read(ramdisk_t *rd, uint32_t offset, uint32_t count, void *buf);
int ramdisk_write(ramdisk_t *rd, uint32_t offset, uint32_t count, const void *buf);
void ramdisk_destroy(ramdisk_t *rd);

#endif
