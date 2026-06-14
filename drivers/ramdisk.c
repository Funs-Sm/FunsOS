#include "ramdisk.h"
#include "string.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"

static ramdisk_t *default_ramdisk;

ramdisk_t *ramdisk_create(uint32_t size) {
    if (size == 0 || size > RAMDISK_MAX_SIZE) return 0;

    ramdisk_t *rd = (ramdisk_t *)kmalloc(sizeof(ramdisk_t));
    if (!rd) return 0;

    rd->data = (uint8_t *)kmalloc(size);
    if (!rd->data) {
        kfree(rd);
        return 0;
    }

    memset(rd->data, 0, size);
    rd->size = size;
    rd->sector_count = size / RAMDISK_SECTOR_SIZE;
    rd->ref_count = 1;

    return rd;
}

int ramdisk_init(uint32_t size) {
    default_ramdisk = ramdisk_create(size);
    if (!default_ramdisk) return -1;
    return 0;
}

int ramdisk_read(ramdisk_t *rd, uint32_t offset, uint32_t count, void *buf) {
    if (!rd || !rd->data || !buf) return -1;
    if (offset + count > rd->size) return -1;

    memcpy(buf, rd->data + offset, count);
    return 0;
}

int ramdisk_write(ramdisk_t *rd, uint32_t offset, uint32_t count, const void *buf) {
    if (!rd || !rd->data || !buf) return -1;
    if (offset + count > rd->size) return -1;

    memcpy(rd->data + offset, buf, count);
    return 0;
}

void ramdisk_destroy(ramdisk_t *rd) {
    if (!rd) return;

    if (rd->ref_count > 1) {
        rd->ref_count--;
        return;
    }

    if (rd->data) {
        kfree(rd->data);
    }
    kfree(rd);
}
