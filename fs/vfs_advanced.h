#ifndef VFS_ADVANCED_H
#define VFS_ADVANCED_H

#include "stdint.h"
#include "vfs.h"

/* 文件锁类型 */
#define FLOCK_NONE      0
#define FLOCK_SHARED    1  /* 共享锁（读锁） */
#define FLOCK_EXCLUSIVE 2  /* 独占锁（写锁） */

/* 文件锁结构 */
typedef struct file_lock {
    uint32_t type;           /* FLOCK_SHARED 或 FLOCK_EXCLUSIVE */
    uint32_t pid;            /* 持有锁的进程ID */
    uint64_t start;          /* 锁定区域起始位置 */
    uint64_t length;         /* 锁定区域长度（0表示到文件末尾） */
    struct file_lock *next;  /* 链表指针 */
} file_lock_t;

/* 扩展属性 (xattr) 结构 */
#define XATTR_NAME_MAX  255
#define XATTR_VALUE_MAX 65536

typedef struct xattr_entry {
    char name[XATTR_NAME_MAX + 1];
    void *value;
    uint32_t value_len;
    struct xattr_entry *next;
} xattr_entry_t;

/* 文件监视类型（inotify） */
#define INOTIFY_ACCESS      0x00000001  /* 文件被访问 */
#define INOTIFY_MODIFY      0x00000002  /* 文件被修改 */
#define INOTIFY_ATTRIB      0x00000004  /* 元数据改变 */
#define INOTIFY_OPEN        0x00000020  /* 文件被打开 */
#define INOTIFY_CLOSE       0x00000018  /* 文件被关闭 */
#define INOTIFY_CREATE      0x00000100  /* 文件/目录被创建 */
#define INOTIFY_DELETE      0x00000200  /* 文件/目录被删除 */
#define INOTIFY_MOVE        0x000000C0  /* 文件/目录被移动 */

typedef struct inotify_event {
    uint32_t mask;           /* 事件类型掩码 */
    uint32_t cookie;         /* 用于关联移动操作 */
    uint32_t ino;            /* inode编号 */
    char name[256];          /* 文件名 */
    struct inotify_event *next;
} inotify_event_t;

/* 文件系统配额结构 */
typedef struct fs_quota {
    uint32_t uid;            /* 用户ID */
    uint32_t gid;            /* 组ID */
    uint64_t block_soft;     /* 软限制（块数） */
    uint64_t block_hard;     /* 硬限制（块数） */
    uint64_t block_used;     /* 已使用块数 */
    uint64_t inode_soft;     /* 软限制（inode数） */
    uint64_t inode_hard;     /* 硬限制（inode数） */
    uint64_t inode_used;     /* 已使用inode数 */
    uint64_t grace_period;   /* 宽限期（秒） */
} fs_quota_t;

/* 文件系统快照结构 */
typedef struct fs_snapshot {
    uint32_t id;             /* 快照ID */
    char name[256];          /* 快照名称 */
    uint64_t timestamp;      /* 创建时间戳 */
    uint64_t size;           /* 快照大小 */
    void *data;              /* 快照数据指针 */
    struct fs_snapshot *next;
} fs_snapshot_t;

/* 文件完整性校验结构 */
typedef struct file_checksum {
    uint32_t algorithm;      /* 校验算法（MD5, SHA256等） */
    uint8_t hash[64];        /* 哈希值 */
    uint32_t hash_len;       /* 哈希长度 */
    uint64_t timestamp;      /* 计算时间戳 */
} file_checksum_t;

/* 文件压缩信息 */
typedef struct file_compression {
    uint32_t algorithm;      /* 压缩算法（GZIP, ZSTD, LZ4等） */
    uint64_t original_size;  /* 原始大小 */
    uint64_t compressed_size;/* 压缩后大小 */
    uint32_t flags;          /* 压缩标志 */
} file_compression_t;

/* 高级文件系统功能API */

/* 文件锁操作 */
int32_t vfs_flock(file_t *file, uint32_t type, uint64_t start, uint64_t length);
int32_t vfs_unflock(file_t *file, uint64_t start, uint64_t length);
int32_t vfs_test_lock(file_t *file, uint64_t start, uint64_t length);

/* 扩展属性操作 */
int32_t vfs_setxattr(const char *path, const char *name, const void *value, uint32_t size, uint32_t flags);
int32_t vfs_getxattr(const char *path, const char *name, void *value, uint32_t size);
int32_t vfs_listxattr(const char *path, char *list, uint32_t size);
int32_t vfs_removexattr(const char *path, const char *name);

/* 文件监视（inotify） */
int32_t vfs_inotify_init(void);
int32_t vfs_inotify_add_watch(int32_t fd, const char *path, uint32_t mask);
int32_t vfs_inotify_rm_watch(int32_t fd, int32_t wd);

/* 配额管理 */
int32_t vfs_quota_set(const char *path, fs_quota_t *quota);
int32_t vfs_quota_get(const char *path, uint32_t uid, fs_quota_t *quota);
int32_t vfs_quota_check(const char *path, uint32_t uid, uint64_t blocks);

/* 快照管理 */
int32_t vfs_snapshot_create(const char *path, const char *name);
int32_t vfs_snapshot_delete(const char *path, uint32_t snapshot_id);
int32_t vfs_snapshot_list(const char *path, fs_snapshot_t **list);
int32_t vfs_snapshot_restore(const char *path, uint32_t snapshot_id);

/* 文件完整性 */
int32_t vfs_checksum_compute(const char *path, uint32_t algorithm, file_checksum_t *checksum);
int32_t vfs_checksum_verify(const char *path, file_checksum_t *checksum);

/* 文件压缩 */
int32_t vfs_compress(const char *path, uint32_t algorithm);
int32_t vfs_decompress(const char *path);
int32_t vfs_get_compression_info(const char *path, file_compression_t *info);

/* 文件克隆（CoW） */
int32_t vfs_clone_file(const char *src, const char *dst);
int32_t vfs_reflink(const char *src, const char *dst, uint64_t offset, uint64_t length);

/* 文件碎片整理 */
int32_t vfs_defrag(const char *path);
int32_t vfs_get_fragmentation(const char *path, uint32_t *frag_percent);

/* 原子操作 */
int32_t vfs_atomic_write(const char *path, const void *buf, uint32_t count);
int32_t vfs_atomic_rename(const char *oldpath, const char *newpath);

/* 文件版本控制 */
int32_t vfs_version_create(const char *path);
int32_t vfs_version_list(const char *path, uint32_t *versions, uint32_t max_count);
int32_t vfs_version_restore(const char *path, uint32_t version);

/* 目录同步 */
int32_t vfs_sync_dir(const char *path);
int32_t vfs_sync_all(void);

/* 文件预读取 */
int32_t vfs_readahead(file_t *file, uint64_t offset, uint64_t length);

/* 直接I/O（绕过缓存） */
int32_t vfs_direct_read(file_t *file, void *buf, uint32_t count);
int32_t vfs_direct_write(file_t *file, const void *buf, uint32_t count);

#endif /* VFS_ADVANCED_H */
