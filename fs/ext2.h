#ifndef EXT2_H
#define EXT2_H

#include "stdint.h"
#include "vfs.h"

#define EXT2_SUPER_MAGIC 0xEF53

#define EXT2_S_IFMT   0xF000
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFLNK  0xA000

typedef struct {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t r_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t log_frag_size;
    uint32_t blocks_per_group;
    uint32_t frags_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mnt_count;
    uint16_t max_mnt_count;
    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minor_rev;
    uint32_t lastcheck;
    uint32_t checkinterval;
    uint32_t creator_os;
    uint32_t rev_level;
    uint32_t def_resuid;
    uint32_t def_resgid;
    uint32_t first_ino;
    uint32_t inode_size;
    uint16_t block_group_nr;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint8_t uuid[16];
    uint8_t volume_name[16];
} __attribute__((packed)) ext2_superblock_t;

typedef struct {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
} __attribute__((packed)) ext2_bgd_t;

typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
} __attribute__((packed)) ext2_inode_t;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[255];
} __attribute__((packed)) ext2_dir_entry_t;

int32_t ext2_init(uint32_t drive, uint32_t partition_start);
int32_t ext2_mount(superblock_t *sb, void *data);
int32_t ext2_read_inode(uint32_t ino, ext2_inode_t *inode);
int32_t ext2_write_inode(uint32_t ino, const ext2_inode_t *inode);
int32_t ext2_read_block(uint32_t block, void *buf);
int32_t ext2_write_block(uint32_t block, const void *buf);
int32_t ext2_truncate(uint32_t ino, uint32_t new_size);
int32_t ext2_chmod(uint32_t ino, uint32_t mode);
int32_t ext2_chown(uint32_t ino, uint32_t uid, uint32_t gid);
int32_t ext2_utimes(uint32_t ino, uint32_t atime, uint32_t mtime);

#endif
