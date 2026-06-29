#ifndef VFS_H
#define VFS_H

#include "stdint.h"
#include "../kernel/permission.h"

#define FS_TYPE_RAMFS  0
#define FS_TYPE_FAT32  1
#define FS_TYPE_EXT2   2
#define FS_TYPE_DEVFS  3
#define FS_TYPE_EXT4   4
#define FS_TYPE_PROCFS 5
#define FS_TYPE_SYSFS  6
#define FS_TYPE_BTRFS  7
#define FS_TYPE_XFS    8
#define FS_TYPE_TARFS  9   /* 补充遗漏的 TARFS 类型 */
#define FS_TYPE_FUSE   10  /* FUSE 用户态文件系统 */

#define FILE_MODE_READ   0x01
#define FILE_MODE_WRITE  0x02
#define FILE_MODE_EXEC   0x04
#define FILE_MODE_DIR    0x08
#define FILE_MODE_REG    0x10
#define FILE_MODE_LNK    0x20
#define FILE_MODE_CREATE 0x40

#define DT_UNKNOWN 0
#define DT_REG     FILE_MODE_REG
#define DT_DIR     FILE_MODE_DIR
#define DT_LNK     FILE_MODE_LNK

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define ENOSYS  38
#define EINVAL  22
#define EBADF   9
#define ENOENT  2
#define EEXIST  17
#define ENOTDIR 20
#define EISDIR  21
#define ENOTEMPTY 39
#define EPERM   1
#define EIO     5
#define ENOMEM  12
#define ENOSPC  28
#define EROFS   30
#define ENODEV  19
#define EBUSY   16

/* ioctl commands */
#define FIONREAD  0x541B
#define FIONBIO   0x5421

/* Directory entry returned by vfs_readdir() */
typedef struct vfs_dirent {
    uint32_t ino;
    uint32_t off;
    uint16_t reclen;
    uint8_t  type;
    char     name[256];
} vfs_dirent_t;

typedef struct inode_t inode_t;
typedef struct superblock_t superblock_t;
typedef struct file_t file_t;
typedef struct dentry_t dentry_t;

typedef struct {
    int32_t (*read_inode)(inode_t *inode);
    int32_t (*write_inode)(inode_t *inode);
    int32_t (*alloc_inode)(superblock_t *sb, inode_t *inode);
    void (*free_inode)(inode_t *inode);
} superblock_ops_t;

struct superblock_t {
    uint32_t fs_type;
    void *fs_data;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    inode_t *root;
    superblock_ops_t *ops;
};

typedef struct {
    int32_t (*open)(inode_t *inode, file_t *file);
    int32_t (*read)(file_t *file, void *buf, uint32_t count);
    int32_t (*write)(file_t *file, const void *buf, uint32_t count);
    int32_t (*close)(file_t *file);
    int32_t (*seek)(file_t *file, int32_t offset, int32_t whence);
    int32_t (*ioctl)(file_t *file, uint32_t cmd, void *arg);
} file_ops_t;

struct file_t {
    inode_t *inode;
    uint32_t offset;
    uint32_t flags;
    file_ops_t *ops;
    void *private_data;
    int32_t ref_count;
};

typedef struct {
    dentry_t *(*lookup)(dentry_t *dir, const char *name);
    int32_t (*create)(dentry_t *dir, const char *name, uint32_t mode);
    int32_t (*mkdir)(dentry_t *dir, const char *name, uint32_t mode);
    int32_t (*unlink)(dentry_t *dir, const char *name);
    int32_t (*rmdir)(dentry_t *dir, const char *name);
    int32_t (*rename)(dentry_t *old_dir, const char *old_name, dentry_t *new_dir, const char *new_name);
    int32_t (*readlink)(dentry_t *dentry, char *buf, uint32_t size);
    int32_t (*symlink)(dentry_t *dir, const char *name, const char *target);
    int32_t (*link)(dentry_t *old_dir, const char *old_name, dentry_t *new_dir, const char *new_name);
} inode_ops_t;

struct inode_t {
    uint32_t ino;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t nlinks;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    superblock_t *sb;
    inode_ops_t *ops;
    void *private_data;
    dentry_t *dentries;
    acl_t *acl;              /* 扩展访问控制列表 (可选) */
};

struct dentry_t {
    char name[256];
    dentry_t *parent;
    dentry_t *child;
    dentry_t *next_sibling;
    inode_t *inode;
    uint32_t mount_point;
    /* Dentry cache LRU list pointers (separate from tree links). */
    dentry_t *cache_prev;
    dentry_t *cache_next;
};

typedef struct mount_t {
    superblock_t *sb;
    dentry_t *mount_point;
    dentry_t *root_dentry;
    struct mount_t *next;
} mount_t;

void vfs_init(void);
int32_t vfs_mount(const char *path, uint32_t fs_type, void *data);
int32_t vfs_umount(const char *path);
int32_t vfs_open(const char *path, uint32_t flags, file_t **file);
int32_t vfs_close(file_t *file);
int32_t vfs_read(file_t *file, void *buf, uint32_t count);
int32_t vfs_write(file_t *file, const void *buf, uint32_t count);
int32_t vfs_seek(file_t *file, int32_t offset, int32_t whence);
int32_t vfs_ioctl(file_t *file, uint32_t cmd, void *arg);
int32_t vfs_mkdir(const char *path, uint32_t mode);
int32_t vfs_rmdir(const char *path);
int32_t vfs_unlink(const char *path);
int32_t vfs_rename(const char *old_path, const char *new_path);
int32_t vfs_stat(const char *path, inode_t *stat);
int32_t vfs_creat(const char *path, uint32_t mode);
int32_t vfs_truncate(const char *path, uint32_t size);
int32_t vfs_access(const char *path, uint32_t mode);
int32_t vfs_symlink(const char *target, const char *linkpath);
int32_t vfs_readlink(const char *path, char *buf, uint32_t size);
int32_t vfs_chmod(const char *path, uint32_t mode);
int32_t vfs_chown(const char *path, uint32_t uid, uint32_t gid);
int32_t vfs_link(const char *oldpath, const char *newpath);
int32_t vfs_utimes(const char *path, uint32_t atime, uint32_t mtime);
int32_t vfs_sync(void);
int32_t vfs_fsync(file_t *file);
int32_t vfs_mknod(const char *path, uint32_t mode, uint32_t dev);

/* Directory iteration */
int32_t vfs_opendir(const char *path, file_t **dir);
int32_t vfs_readdir(file_t *dir, vfs_dirent_t *entry);
int32_t vfs_closedir(file_t *dir);

/* Per-process (or global for now) working directory */
int32_t vfs_chdir(const char *path);
const char *vfs_getcwd(void);

#endif
