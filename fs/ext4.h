#ifndef EXT4_H
#define EXT4_H

#include "stdint.h"
#include "ext2.h"

#define EXT4_SUPER_MAGIC 0xEF53

#define EXT4_FEATURE_COMPAT_HAS_JOURNAL 0x4
#define EXT4_FEATURE_INCOMPAT_EXTENTS   0x40
#define EXT4_FEATURE_INCOMPAT_FLEX_BG   0x200

typedef struct {
    uint32_t ei_block;
    uint32_t ei_leaf_lo;
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
} __attribute__((packed)) ext4_extent_idx;

typedef struct {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
} __attribute__((packed)) ext4_extent;

typedef struct {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint16_t eh_generation;
} __attribute__((packed)) ext4_extent_header;

typedef struct {
    uint32_t magic;
    uint32_t block_type;
    uint32_t block_seq;
    uint32_t block_size;
} ext4_journal_header_t;

#define EXT4_JOURNAL_MAGIC 0xC03B3998

/* Journal statistics */
typedef struct {
    uint32_t commits;
    uint32_t rollbacks;
    uint32_t blocks_written;
    uint32_t replays;
    uint32_t checkpoints;
    uint32_t last_seq;
} ext4_journal_stats_t;

int32_t ext4_init(uint32_t drive, uint32_t partition_start);
int32_t ext4_mount(superblock_t *sb, void *data);
int32_t ext4_read_extent(inode_t *inode, void *buf, uint32_t offset, uint32_t count);
int32_t ext4_write_extent(inode_t *inode, const void *buf, uint32_t offset, uint32_t count);
void ext4_journal_recover(void);
int32_t ext4_journal_write(uint32_t block, const void *data);
int32_t journal_replay(void);
int32_t journal_checkpoint(void);
ext4_journal_stats_t *journal_get_stats(void);

/* Expose file_ops for VFS integration */
extern file_ops_t ext4_file_ops;  /* defined in ext4.c as non-static */

int32_t ext4_chmod(uint32_t ino, uint32_t mode);
int32_t ext4_chown(uint32_t ino, uint32_t uid, uint32_t gid);
int32_t ext4_utimes(uint32_t ino, uint32_t atime, uint32_t mtime);

#endif
