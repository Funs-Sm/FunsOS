#ifndef VFS_EXT_H
#define VFS_EXT_H

#include "stdint.h"
#include "vfs.h"

/* 扩展 VFS 节点类型 */
#define VFS_EXT_TYPE_FILE     0
#define VFS_EXT_TYPE_DIR      1
#define VFS_EXT_TYPE_SYMLINK  2
#define VFS_EXT_TYPE_DEVICE   3
#define VFS_EXT_TYPE_PIPE     4
#define VFS_EXT_TYPE_SOCKET   5

/* 打开标志 */
#define VFS_EXT_O_RDONLY   0x01
#define VFS_EXT_O_WRONLY   0x02
#define VFS_EXT_O_RDWR     0x03
#define VFS_EXT_O_CREAT    0x04
#define VFS_EXT_O_TRUNC    0x08
#define VFS_EXT_O_APPEND   0x10

/* 挂载标志 */
#define VFS_EXT_MNT_RDONLY  0x01
#define VFS_EXT_MNT_NOSUID  0x02
#define VFS_EXT_MNT_NOEXEC  0x04
#define VFS_EXT_MNT_NOATIME 0x08

/* 最大文件描述符数 */
#define VFS_EXT_MAX_FDS      256
#define VFS_EXT_MAX_PATH     256
#define VFS_EXT_MAX_DEVICE   64
#define VFS_EXT_MAX_FSTYPE   32

/* 扩展 VFS 节点 */
typedef struct vfs_ext_node {
    char path[256];
    uint32_t inode;
    uint32_t type;
    uint32_t size;
    uint32_t permissions;
    uint32_t owner_uid;
    uint32_t group_gid;
    uint32_t created;
    uint32_t modified;
    uint32_t accessed;
    uint32_t ref_count;
    struct vfs_ext_node *parent;
    struct vfs_ext_node *children;
    struct vfs_ext_node *next;
    void *fs_data;
} vfs_ext_node_t;

/* 扩展挂载点 */
typedef struct vfs_ext_mount {
    char mount_point[256];
    char device[64];
    char fs_type[32];
    uint32_t flags;
    vfs_ext_node_t *root;
    struct vfs_ext_mount *next;
} vfs_ext_mount_t;

/* 文件描述符条目 */
typedef struct vfs_ext_fd {
    int fd;
    vfs_ext_node_t *node;
    uint32_t offset;
    uint32_t flags;
    int in_use;
} vfs_ext_fd_t;

/* stat 缓冲区 */
typedef struct vfs_ext_stat {
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_size;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_atime;
    uint32_t st_mtime;
    uint32_t st_ctime;
    uint32_t st_blksize;
    uint32_t st_blocks;
} vfs_ext_stat_t;

/* 目录条目（readdir 用） */
typedef struct vfs_ext_dirent {
    uint32_t d_ino;
    char d_name[256];
    uint32_t d_type;
} vfs_ext_dirent_t;

/* 配额结构 */
typedef struct vfs_ext_quota {
    uint32_t uid;
    uint32_t soft_limit;
    uint32_t hard_limit;
    uint32_t used;
} vfs_ext_quota_t;

/* ---- API 声明 ---- */

void vfs_ext_init(void);

/* 挂载/卸载 */
int vfs_ext_mount(const char *device, const char *mount_point, const char *fs_type, uint32_t flags);
int vfs_ext_unmount(const char *mount_point);

/* 路径解析 */
vfs_ext_node_t *vfs_ext_resolve(const char *path);

/* 目录操作 */
int vfs_ext_mkdir(const char *path, uint32_t mode);
int vfs_ext_rmdir(const char *path);
int vfs_ext_readdir(const char *path, void *buf, int max_entries);

/* 文件操作 */
int vfs_ext_create(const char *path, uint32_t mode);
int vfs_ext_remove(const char *path);
int vfs_ext_rename(const char *old_path, const char *new_path);
int vfs_ext_truncate(const char *path, uint32_t size);
int vfs_ext_stat(const char *path, void *stat_buf);

/* 符号链接 */
int vfs_ext_symlink(const char *target, const char *link_path);
int vfs_ext_readlink(const char *path, char *buf, int bufsize);

/* 权限 */
int vfs_ext_chmod(const char *path, uint32_t mode);
int vfs_ext_chown(const char *path, uint32_t uid, uint32_t gid);

/* 路径操作 */
int vfs_ext_getcwd(char *buf, int bufsize);
int vfs_ext_chdir(const char *path);

/* 文件描述符表 */
int vfs_ext_open(const char *path, int flags, int mode);
int vfs_ext_close(int fd);
int vfs_ext_read(int fd, void *buf, int count);
int vfs_ext_write(int fd, const void *buf, int count);
int vfs_ext_seek(int fd, int offset, int whence);
int vfs_ext_dup(int old_fd);
int vfs_ext_dup2(int old_fd, int new_fd);

/* 磁盘配额 */
int vfs_ext_set_quota(const char *path, uint32_t uid, uint32_t soft_limit, uint32_t hard_limit);
int vfs_ext_get_quota(const char *path, uint32_t uid, uint32_t *used, uint32_t *soft, uint32_t *hard);

/* ================================================================ */
/*  1) Journal/Transaction 支持                                       */
/* ================================================================ */

#define VFS_EXT_JOURNAL_MAX_ENTRIES 512
#define VFS_EXT_JOURNAL_OP_CREATE   1
#define VFS_EXT_JOURNAL_OP_REMOVE   2
#define VFS_EXT_JOURNAL_OP_RENAME   3
#define VFS_EXT_JOURNAL_OP_WRITE    4
#define VFS_EXT_JOURNAL_OP_TRUNCATE 5
#define VFS_EXT_JOURNAL_OP_MKDIR    6
#define VFS_EXT_JOURNAL_OP_RMDIR    7
#define VFS_EXT_JOURNAL_OP_SYMLINK  8
#define VFS_EXT_JOURNAL_OP_CHMOD    9
#define VFS_EXT_JOURNAL_OP_CHOWN    10
#define VFS_EXT_JOURNAL_STATE_PENDING  0
#define VFS_EXT_JOURNAL_STATE_COMMIT   1
#define VFS_EXT_JOURNAL_STATE_ROLLBACK 2

typedef struct vfs_ext_journal_entry {
    uint32_t entry_id;
    uint32_t operation;
    uint32_t state;
    uint32_t timestamp;
    char path[256];
    char target_path[256];
    uint32_t inode;
    uint32_t old_size;
    uint32_t new_size;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint8_t data[512];
    uint32_t data_len;
} vfs_ext_journal_entry_t;

typedef struct vfs_ext_journal {
    vfs_ext_journal_entry_t entries[512];
    uint32_t entry_count;
    uint32_t next_entry_id;
    int active;
} vfs_ext_journal_t;

/* ---- Journal/Transaction API ---- */
int  vfs_ext_journal_init(void);
int  vfs_ext_journal_begin(void);
int  vfs_ext_journal_log(uint32_t op, const char *path, const char *target, uint32_t inode, uint32_t mode);
int  vfs_ext_journal_commit(void);
int  vfs_ext_journal_rollback(void);
int  vfs_ext_journal_replay(void);

/* ================================================================ */
/*  2) File Locking (FLOCK)                                          */
/* ================================================================ */

#define VFS_EXT_LOCK_SHARED    0x01
#define VFS_EXT_LOCK_EXCLUSIVE 0x02
#define VFS_EXT_LOCK_NONBLOCK  0x04
#define VFS_EXT_MAX_LOCKS      128

typedef struct vfs_ext_flock {
    uint32_t lock_id;
    uint32_t inode;
    uint32_t owner_uid;
    uint32_t lock_type;
    uint32_t lock_start;
    uint32_t lock_len;
    uint32_t created;
    int active;
} vfs_ext_flock_t;

int  vfs_ext_flock_init(void);
int  vfs_ext_flock_acquire(uint32_t inode, uint32_t owner_uid, uint32_t type, uint32_t start, uint32_t len);
int  vfs_ext_flock_release(uint32_t inode, uint32_t owner_uid);
int  vfs_ext_flock_test(uint32_t inode, uint32_t owner_uid, uint32_t *conflict_uid);
int  vfs_ext_flock_release_all(uint32_t owner_uid);

/* ================================================================ */
/*  3) Disk Cache Management (LRU, read-ahead, write-back)           */
/* ================================================================ */

#define VFS_EXT_CACHE_BLOCK_SIZE  4096
#define VFS_EXT_CACHE_MAX_BLOCKS  256

typedef struct vfs_ext_cache_block {
    uint32_t block_id;
    uint32_t inode;
    uint32_t offset;
    uint32_t size;
    uint8_t data[4096];
    int dirty;
    uint32_t access_time;
    int valid;
} vfs_ext_cache_block_t;

typedef struct vfs_ext_block_cache {
    vfs_ext_cache_block_t blocks[256];
    uint32_t block_count;
} vfs_ext_block_cache_t;

int  vfs_ext_cache_init(void);
int  vfs_ext_cache_read(uint32_t inode, uint32_t offset, void *buf, uint32_t size);
int  vfs_ext_cache_write(uint32_t inode, uint32_t offset, const void *buf, uint32_t size);
int  vfs_ext_cache_flush(void);
int  vfs_ext_cache_flush_inode(uint32_t inode);
int  vfs_ext_cache_invalidate(uint32_t inode);
int  vfs_ext_cache_read_ahead(uint32_t inode, uint32_t offset, uint32_t count);
int  vfs_ext_cache_write_back(uint32_t inode);
void vfs_ext_cache_print_stats(void);

/* ================================================================ */
/*  4) Inode Management (bitmap tracking)                            */
/* ================================================================ */

#define VFS_EXT_INODE_BITMAP_SIZE  4096
#define VFS_EXT_MAX_INODES         (VFS_EXT_INODE_BITMAP_SIZE * 8)

typedef struct vfs_ext_inode_table {
    uint8_t bitmap[4096];
    uint32_t total_inodes;
    uint32_t free_inodes;
    uint32_t next_free_hint;
} vfs_ext_inode_table_t;

int      vfs_ext_inode_table_init(void);
uint32_t vfs_ext_inode_alloc(void);
int      vfs_ext_inode_free(uint32_t inode);
int      vfs_ext_inode_is_allocated(uint32_t inode);
uint32_t vfs_ext_inode_get_free_count(void);
void     vfs_ext_inode_dump_usage(void);

/* ================================================================ */
/*  5) Extended Attributes (xattr)                                   */
/* ================================================================ */

#define VFS_EXT_XATTR_MAX_NAME   128
#define VFS_EXT_XATTR_MAX_VALUE  1024
#define VFS_EXT_XATTR_MAX_ENTRIES 64

typedef struct vfs_ext_xattr_entry {
    uint32_t inode;
    char name[128];
    char value[1024];
    uint32_t value_len;
    int active;
} vfs_ext_xattr_entry_t;

int vfs_ext_xattr_set(uint32_t inode, const char *name, const void *value, uint32_t size);
int vfs_ext_xattr_get(uint32_t inode, const char *name, void *buf, uint32_t *size);
int vfs_ext_xattr_remove(uint32_t inode, const char *name);
int vfs_ext_xattr_list(uint32_t inode, char *buf, uint32_t bufsize);
int vfs_ext_xattr_copy(uint32_t src_inode, uint32_t dst_inode);

/* ================================================================ */
/*  6) File System Check (fsck)                                      */
/* ================================================================ */

#define VFS_EXT_FSCK_ERROR_ORPHAN     1
#define VFS_EXT_FSCK_ERROR_CROSSLINK  2
#define VFS_EXT_FSCK_ERROR_BAD_TYPE   3
#define VFS_EXT_FSCK_ERROR_CYCLE      4
#define VFS_EXT_FSCK_MAX_ERRORS       128

typedef struct vfs_ext_fsck_error {
    uint32_t error_type;
    uint32_t inode;
    char path[256];
    char detail[256];
} vfs_ext_fsck_error_t;

typedef struct vfs_ext_fsck_result {
    int passed;
    int errors_found;
    int errors_fixed;
    int warnings;
    vfs_ext_fsck_error_t errors[128];
} vfs_ext_fsck_result_t;

vfs_ext_fsck_result_t vfs_ext_fsck(void);
int vfs_ext_fsck_fix_orphans(void);
int vfs_ext_fsck_fix_crosslinks(void);

/* ================================================================ */
/*  7) Memory-Mapped File I/O (mmap)                                 */
/* ================================================================ */

#define VFS_EXT_MMAP_MAX_REGIONS  64
#define VFS_EXT_MMAP_PROT_READ    0x01
#define VFS_EXT_MMAP_PROT_WRITE   0x02
#define VFS_EXT_MMAP_PROT_EXEC    0x04
#define VFS_EXT_MMAP_MAP_SHARED   0x01
#define VFS_EXT_MMAP_MAP_PRIVATE  0x02
#define VFS_EXT_MMAP_MAP_ANON     0x04
#define VFS_EXT_MMAP_MAP_FIXED    0x08

typedef struct vfs_ext_mmap_region {
    uint32_t region_id;
    uint32_t inode;
    void *virt_addr;
    uint32_t offset;
    uint32_t size;
    uint32_t prot;
    uint32_t flags;
    int active;
} vfs_ext_mmap_region_t;

int  vfs_ext_mmap_init(void);
int  vfs_ext_mmap_map(uint32_t inode, void *addr, uint32_t offset, uint32_t size, uint32_t prot, uint32_t flags, void **out_addr);
int  vfs_ext_mmap_unmap(void *addr, uint32_t size);
int  vfs_ext_mmap_sync(void *addr, uint32_t size, int async);
int  vfs_ext_mmap_protect(void *addr, uint32_t size, uint32_t prot);

/* ================================================================ */
/*  8) Async I/O (aio)                                               */
/* ================================================================ */

#define VFS_EXT_AIO_MAX_REQUESTS 64
#define VFS_EXT_AIO_READ          1
#define VFS_EXT_AIO_WRITE         2
#define VFS_EXT_AIO_STATE_PENDING 0
#define VFS_EXT_AIO_STATE_RUNNING  1
#define VFS_EXT_AIO_STATE_DONE    2
#define VFS_EXT_AIO_STATE_ERROR   3

typedef void (*vfs_ext_aio_callback_t)(int result, void *user_data);

typedef struct vfs_ext_aio_request {
    uint32_t request_id;
    uint32_t inode;
    uint32_t type;
    uint32_t offset;
    void *buffer;
    uint32_t size;
    uint32_t state;
    int result;
    vfs_ext_aio_callback_t callback;
    void *user_data;
    uint32_t submitted;
    uint32_t completed;
    int active;
} vfs_ext_aio_request_t;

int  vfs_ext_aio_init(void);
int  vfs_ext_aio_read(uint32_t inode, void *buf, uint32_t offset, uint32_t size, vfs_ext_aio_callback_t cb, void *user_data, uint32_t *req_id);
int  vfs_ext_aio_write(uint32_t inode, const void *buf, uint32_t offset, uint32_t size, vfs_ext_aio_callback_t cb, void *user_data, uint32_t *req_id);
int  vfs_ext_aio_wait(uint32_t req_id);
int  vfs_ext_aio_poll(uint32_t req_id, int *done, int *result);
int  vfs_ext_aio_cancel(uint32_t req_id);
int  vfs_ext_aio_process_all(void);
int  vfs_ext_aio_cleanup(void);

#endif /* VFS_EXT_H */