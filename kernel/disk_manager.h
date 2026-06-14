#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include "stdint.h"

#define DISK_TYPE_UNKNOWN  0
#define DISK_TYPE_AHCI     1
#define DISK_TYPE_NVME     2
#define DISK_TYPE_IDE      3
#define DISK_TYPE_USB      4
#define DISK_TYPE_RAMDISK  5
#define DISK_TYPE_VIRTIO   6

#define DISK_FLAG_READONLY  0x01
#define DISK_FLAG_REMOVABLE 0x02

typedef struct disk_info {
    char name[16];          /* e.g., "sda", "sdb", "hda" */
    uint32_t type;
    uint32_t flags;
    uint64_t sector_count;
    uint32_t sector_size;
    uint32_t block_size;
    uint8_t pci_bus, pci_dev, pci_func;
    void *driver_data;

    /* Operations */
    int (*read_sectors)(struct disk_info *disk, uint64_t lba, uint32_t count, void *buf);
    int (*write_sectors)(struct disk_info *disk, uint64_t lba, uint32_t count, const void *buf);
    int (*ioctl)(struct disk_info *disk, uint32_t cmd, void *arg);
} disk_info_t;

typedef struct partition_info {
    char name[20];          /* e.g., "sda1", "sda2" */
    uint8_t disk_index;
    uint8_t part_number;
    uint8_t type;           /* MBR partition type */
    uint8_t boot_flag;
    uint64_t start_lba;
    uint64_t sector_count;
    disk_info_t *disk;
} partition_info_t;

void disk_manager_init(void);
int disk_register(disk_info_t *disk);
int disk_unregister(const char *name);
disk_info_t *disk_find_by_name(const char *name);
uint32_t disk_get_count(void);
disk_info_t *disk_get_by_index(uint32_t index);
int disk_read(const char *name, uint64_t lba, uint32_t count, void *buf);
int disk_write(const char *name, uint64_t lba, uint32_t count, const void *buf);
int disk_detect_partitions(disk_info_t *disk);
uint32_t partition_get_count(void);
partition_info_t *partition_get_by_index(uint32_t index);

#endif
