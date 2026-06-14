#ifndef BTRFS_H
#define BTRFS_H

#include "vfs.h"

/* btrfs 超级块魔数 */
#define BTRFS_MAGIC  0x4D5F53665348425FULL  /* "_BHRfS_M" */

/* btrfs 超级块结构 (位于偏移 64KB = 65536) */
typedef struct {
    uint8_t  fsid[16];          /* 文件系统 UUID */
    uint64_t bytenr;            /* 此超级块的字节偏移 */
    uint64_t flags;
    uint64_t magic;             /* BTRFS_MAGIC */
    uint64_t generation;
    uint64_t root;              /* 根树根节点字节偏移 */
    uint64_t chunk_root;        /* chunk 树根节点字节偏移 */
    uint64_t log_root;
    uint64_t total_bytes;       /* 文件系统总大小 */
    uint64_t bytes_used;        /* 已使用字节数 */
    uint64_t root_dir_objectid; /* 根目录对象ID */
    uint32_t nodesize;          /* 节点大小(通常4096) */
    uint32_t leafsize;          /* 叶节点大小 */
    uint32_t stripesize;
    uint32_t sectorsize;        /* 扇区大小(通常4096) */
    uint32_t csum_type;         /* 校验和类型(0=CRC32C) */
    uint64_t cache_generation;
    char     label[256];        /* 文件系统标签 */
    uint64_t dev_item_devid;
    uint64_t dev_item_total_bytes;
    /* ... 其他字段省略 ... */
} btrfs_superblock_t;

/* btrfs B-tree 节点头 */
typedef struct {
    uint8_t  csum[32];
    uint8_t  fsid[16];
    uint64_t bytenr;
    uint64_t flags;
    uint8_t  backref_rev;
    uint8_t  level;             /* 树层级(0=叶节点) */
    uint32_t items;             /* 项目数 */
    uint64_t generation;
    uint64_t owner;
} btrfs_header_t;

/* btrfs 项目 */
typedef struct {
    uint64_t key_objectid;
    uint8_t  key_type;
    uint64_t key_offset;
    uint32_t offset;            /* 数据在叶节点中的偏移 */
    uint32_t size;              /* 数据大小 */
} btrfs_item_t;

/* 关键类型常量 */
#define BTRFS_INODE_ITEM_KEY     1
#define BTRFS_DIR_ITEM_KEY      84
#define BTRFS_DIR_INDEX_KEY      96
#define BTRFS_EXTENT_DATA_KEY   108
#define BTRFS_ROOT_ITEM_KEY     132
#define BTRFS_ROOT_BACKREF_KEY  144

/* inode 标志 */
#define BTRFS_FT_REG_FILE    1
#define BTRFS_FT_DIR         2
#define BTRFS_FT_CHRDEV      3
#define BTRFS_FT_BLKDEV      4
#define BTRFS_FT_FIFO        5
#define BTRFS_FT_SOCK        6
#define BTRFS_FT_SYMLINK     7

/* btrfs 内部 inode 节点 (内存中表示) */
typedef struct btrfs_inode_info {
    uint64_t objectid;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint32_t nlink;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    /* extent 数据位置信息 */
    uint64_t extent_bytenr;
    uint64_t extent_size;
} btrfs_inode_info_t;

/* btrfs 目录项 (内存中表示) */
typedef struct btrfs_dir_entry {
    uint64_t objectid;
    uint8_t  type;
    uint32_t name_len;
    char     name[256];
    struct btrfs_dir_entry *next;
} btrfs_dir_entry_t;

int btrfs_init(void);
int btrfs_mount_device(const char *path, uint32_t device_id);
int btrfs_read_file(const char *path, void *buf, uint32_t size);
int btrfs_list_dir(const char *path);

#endif
