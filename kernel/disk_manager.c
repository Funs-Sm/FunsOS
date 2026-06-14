#include "disk_manager.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"

#define MAX_DISKS       16
#define MAX_PARTITIONS  64

/* MBR partition table entry */
typedef struct {
    uint8_t  boot_flag;       /* 0x80 = bootable */
    uint8_t  chs_start[3];   /* CHS of first sector */
    uint8_t  type;            /* Partition type */
    uint8_t  chs_end[3];     /* CHS of last sector */
    uint32_t lba_start;      /* LBA of first sector */
    uint32_t sectors;         /* Number of sectors */
} __attribute__((packed)) mbr_part_entry_t;

static disk_info_t *disk_list[MAX_DISKS];
static uint32_t disk_count = 0;

static partition_info_t partition_list[MAX_PARTITIONS];
static uint32_t partition_count = 0;

void disk_manager_init(void) {
    memset(disk_list, 0, sizeof(disk_list));
    memset(partition_list, 0, sizeof(partition_list));
    disk_count = 0;
    partition_count = 0;
}

int disk_register(disk_info_t *disk) {
    if (!disk) return -1;
    if (disk_count >= MAX_DISKS) return -1;

    /* Check for duplicate name */
    for (uint32_t i = 0; i < disk_count; i++) {
        if (disk_list[i] && strcmp(disk_list[i]->name, disk->name) == 0) {
            return -1;
        }
    }

    disk_list[disk_count] = disk;
    disk_count++;

    /* Auto-detect partitions */
    disk_detect_partitions(disk);

    return 0;
}

int disk_unregister(const char *name) {
    if (!name) return -1;

    for (uint32_t i = 0; i < disk_count; i++) {
        if (disk_list[i] && strcmp(disk_list[i]->name, name) == 0) {
            /* Remove partitions belonging to this disk */
            uint8_t disk_idx = (uint8_t)i;
            for (uint32_t p = 0; p < partition_count; ) {
                if (partition_list[p].disk_index == disk_idx) {
                    /* Shift remaining partitions */
                    for (uint32_t k = p; k < partition_count - 1; k++) {
                        partition_list[k] = partition_list[k + 1];
                    }
                    partition_count--;
                } else {
                    /* Update disk_index for partitions after the removed disk */
                    if (partition_list[p].disk_index > disk_idx) {
                        partition_list[p].disk_index--;
                    }
                    p++;
                }
            }

            /* Shift remaining disk entries */
            for (uint32_t j = i; j < disk_count - 1; j++) {
                disk_list[j] = disk_list[j + 1];
            }
            disk_list[disk_count - 1] = NULL;
            disk_count--;

            return 0;
        }
    }

    return -1;
}

disk_info_t *disk_find_by_name(const char *name) {
    if (!name) return NULL;

    for (uint32_t i = 0; i < disk_count; i++) {
        if (disk_list[i] && strcmp(disk_list[i]->name, name) == 0) {
            return disk_list[i];
        }
    }
    return NULL;
}

uint32_t disk_get_count(void) {
    return disk_count;
}

disk_info_t *disk_get_by_index(uint32_t index) {
    if (index >= disk_count) return NULL;
    return disk_list[index];
}

int disk_read(const char *name, uint64_t lba, uint32_t count, void *buf) {
    disk_info_t *disk = disk_find_by_name(name);
    if (!disk) return -1;
    if (!disk->read_sectors) return -1;
    return disk->read_sectors(disk, lba, count, buf);
}

int disk_write(const char *name, uint64_t lba, uint32_t count, const void *buf) {
    disk_info_t *disk = disk_find_by_name(name);
    if (!disk) return -1;
    if (!disk->write_sectors) return -1;
    if (disk->flags & DISK_FLAG_READONLY) return -1;
    return disk->write_sectors(disk, lba, count, buf);
}

int disk_detect_partitions(disk_info_t *disk) {
    if (!disk) return -1;
    if (!disk->read_sectors) return -1;

    /* Allocate temporary buffer for MBR read */
    uint8_t *mbr = (uint8_t *)kmalloc(512);
    if (!mbr) return -1;

    /* Read sector 0 (MBR) */
    int ret = disk->read_sectors(disk, 0, 1, mbr);
    if (ret != 0) {
        kfree(mbr);
        return -1;
    }

    /* Check MBR signature 0xAA55 at offset 510 */
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        kfree(mbr);
        return -1; /* Not a valid MBR */
    }

    /* Find the disk index for partition references */
    uint8_t disk_idx = 0;
    for (uint32_t i = 0; i < disk_count; i++) {
        if (disk_list[i] == disk) {
            disk_idx = (uint8_t)i;
            break;
        }
    }

    /* Parse 4 partition entries at offset 446 */
    mbr_part_entry_t *entries = (mbr_part_entry_t *)(mbr + 446);

    for (int p = 0; p < 4; p++) {
        mbr_part_entry_t *entry = &entries[p];

        /* Skip empty entries (type 0 means unused) */
        if (entry->type == 0) continue;
        if (entry->sectors == 0) continue;

        if (partition_count >= MAX_PARTITIONS) break;

        partition_info_t *part = &partition_list[partition_count];
        memset(part, 0, sizeof(partition_info_t));

        /* Build partition name: disk_name + partition_number */
        snprintf(part->name, sizeof(part->name), "%s%d", disk->name, p + 1);

        part->disk_index = disk_idx;
        part->part_number = (uint8_t)(p + 1);
        part->type = entry->type;
        part->boot_flag = entry->boot_flag;
        part->start_lba = entry->lba_start;
        part->sector_count = entry->sectors;
        part->disk = disk;

        partition_count++;
    }

    kfree(mbr);
    return 0;
}

uint32_t partition_get_count(void) {
    return partition_count;
}

partition_info_t *partition_get_by_index(uint32_t index) {
    if (index >= partition_count) return NULL;
    return &partition_list[index];
}
