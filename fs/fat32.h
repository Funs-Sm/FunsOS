#ifndef FAT32_H
#define FAT32_H

#include "stdint.h"
#include "vfs.h"

typedef struct {
    uint8_t jmp[3];
    uint8_t oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_sig;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
} __attribute__((packed)) FAT32_BOOT_SECTOR;

typedef struct {
    uint8_t name[8];
    uint8_t ext[3];
    uint8_t attrs;
    uint8_t reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_lo;
    uint32_t file_size;
} __attribute__((packed)) FAT32_DIR_ENTRY;

typedef struct {
    uint8_t order;
    uint8_t name1[10];
    uint8_t attrs;
    uint8_t type;
    uint8_t checksum;
    uint8_t name2[12];
    uint16_t cluster;
    uint8_t name3[4];
} __attribute__((packed)) FAT32_LFN_ENTRY;

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F

#define FAT32_EOC 0x0FFFFFF8
#define FAT32_FREE 0x00000000
#define FAT32_BAD  0x0FFFFFF7

typedef struct {
    uint32_t root_cluster;
    uint32_t fat_start;
    uint32_t data_start;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_sector;
    uint32_t total_clusters;
    uint32_t fs_sector_start;
} fat32_info_t;

int32_t fat32_init(uint32_t drive, uint32_t partition_start);
int32_t fat32_mount(superblock_t *sb, void *data);
int32_t fat32_read(inode_t *inode, void *buf, uint32_t count, uint32_t offset);
int32_t fat32_write(inode_t *inode, const void *buf, uint32_t count, uint32_t offset);
uint32_t fat32_open_dir(uint32_t cluster);
int32_t fat32_read_dir(uint32_t cluster, FAT32_DIR_ENTRY *entries, uint32_t max);
int32_t fat32_find_entry(uint32_t dir_cluster, const char *name, FAT32_DIR_ENTRY *out);
int32_t fat32_get_cluster_chain(uint32_t start, uint32_t *chain, uint32_t max);

#endif
