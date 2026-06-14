#ifndef XFS_H
#define XFS_H

#include "vfs.h"

/* XFS 超级块魔数 */
#define XFS_MAGIC  0x58465342ULL  /* "XFSB" */

/* XFS 超级块结构 (位于偏移 0) */
typedef struct {
    uint32_t magic;             /* XFS_MAGIC */
    uint32_t blocksize;         /* 块大小(通常4096) */
    uint64_t dblocks;           /* 数据块总数 */
    uint64_t rblocks;           /* 实时块数 */
    uint32_t agcount;           /* 分配组数量 */
    uint32_t agsize;            /* 每个AG的块数 */
    uint32_t sectsize;          /* 扇区大小 */
    uint32_t inodesize;         /* inode大小(通常256或512) */
    uint32_t inopblock;         /* 每块inode数 */
    char     fname[12];         /* 文件系统标签 */
    uint64_t rootino;           /* 根目录inode号 */
    /* ... 其他字段 ... */
} xfs_superblock_t;

/* XFS inode 核心 */
typedef struct {
    uint16_t magic;             /* 0x494E = "IN" */
    uint16_t mode;              /* 文件类型+权限 */
    int32_t  nlink;             /* 硬链接数 */
    uint32_t uid, gid;
    uint64_t size;              /* 文件大小 */
    uint64_t atime, mtime, ctime;
    /* ...extent 数据等... */
} xfs_inode_core_t;

/* XFS 内部 inode 信息 (内存中表示) */
typedef struct xfs_inode_info {
    uint64_t ino;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint32_t nlink;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    /* extent 映射 */
    uint64_t extent_start;
    uint64_t extent_count;
} xfs_inode_info_t;

/* XFS 目录项 (内存中表示) */
typedef struct xfs_dir_entry {
    uint64_t ino;
    uint8_t  type;
    uint32_t name_len;
    char     name[256];
    struct xfs_dir_entry *next;
} xfs_dir_entry_t;

int xfs_init(void);
int xfs_mount_device(const char *path, uint32_t device_id);
int xfs_read_file(const char *path, void *buf, uint32_t size);
int xfs_list_dir(const char *path);

#endif
