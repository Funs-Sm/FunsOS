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

#endif /* VFS_EXT_H */