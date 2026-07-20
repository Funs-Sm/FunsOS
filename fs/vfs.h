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
#define FS_TYPE_MINIX  11  /* MINIX 文件系统 */
#define FS_TYPE_REISERFS 12 /* ReiserFS 文件系统 */
#define FS_TYPE_REISER4  13 /* Reiser4 文件系统 */
#define FS_TYPE_ZFS    14  /* ZFS 文件系统 */
#define FS_TYPE_UFS    15  /* UFS (Unix File System) */
#define FS_TYPE_JFS    16  /* JFS (IBM Journaled File System) */
#define FS_TYPE_HFS    17  /* HFS (Apple Hierarchical File System) */
#define FS_TYPE_HFSPLUS 18 /* HFS+ 文件系统 */
#define FS_TYPE_APFS   19  /* APFS (Apple File System) */
#define FS_TYPE_NTFS   20  /* NTFS 文件系统 */
#define FS_TYPE_EXFAT  21  /* exFAT 文件系统 */
#define FS_TYPE_ISO9660 22 /* ISO 9660 (CD-ROM) */
#define FS_TYPE_UDF    23  /* UDF (DVD) */
#define FS_TYPE_SQUASHFS 24 /* SquashFS 压缩文件系统 */
#define FS_TYPE_CRAMFS 25  /* CramFS 压缩文件系统 */
#define FS_TYPE_JFFS2  26  /* JFFS2 (Flash 文件系统) */
#define FS_TYPE_YAFFS2 27  /* YAFFS2 (NAND Flash) */
#define FS_TYPE_UBIFS  28  /* UBIFS (Unsorted Block Image) */
#define FS_TYPE_LOGFS  29  /* LogFS */
#define FS_TYPE_NILFS  30  /* NILFS (New Implementation of a Log-structured FS) */
#define FS_TYPE_FFS    31  /* FFS (Fast File System) */
#define FS_TYPE_LUSTRE 32  /* Lustre 集群文件系统 */
#define FS_TYPE_CEPH   33  /* Ceph 分布式文件系统 */
#define FS_TYPE_GPFS   34  /* GPFS (IBM General Parallel File System) */
#define FS_TYPE_OCFS2  35  /* OCFS2 (Oracle Cluster File System) */
#define FS_TYPE_GFS2   36  /* GFS2 (Global File System 2) */
#define FS_TYPE_XFS2   37  /* XFS v2 */
#define FS_TYPE_BFS    38  /* BFS (Be File System) */
#define FS_TYPE_SYSV   39  /* System V 文件系统 */
#define FS_TYPE_COHERENT 40 /* Coherent 文件系统 */
#define FS_TYPE_QNX4   41  /* QNX4 文件系统 */
#define FS_TYPE_QNX6   42  /* QNX6 文件系统 */
#define FS_TYPE_AFFS   43  /* Amiga Fast File System */
#define FS_TYPE_ADFS   44  /* Acorn Disc Filing System */
#define FS_TYPE_HPFS   45  /* HPFS (High Performance File System) */
#define FS_TYPE_VXFS   46  /* VxFS (Veritas File System) */
#define FS_TYPE_F2FS   47  /* F2FS (Flash-Friendly File System) */
#define FS_TYPE_ORANGEFS 48 /* OrangeFS (PVFS) */
#define FS_TYPE_GLUSTERFS 49 /* GlusterFS */
#define FS_TYPE_TMPFS  50  /* TMPFS (in-memory filesystem) */
#define FS_TYPE_COUNT  51  /* 文件系统类型总数 */

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
    uint32_t total_inodes;
    uint32_t free_inodes;
    uint32_t mount_flags;
    inode_t *root;
    superblock_ops_t *ops;
};

/* 挂载标志 */
#define MS_RDONLY     0x0001  /* 只读挂载 */
#define MS_NOSUID     0x0002  /* 不允许 setuid/setgid */
#define MS_NODEV      0x0004  /* 不允许访问设备文件 */
#define MS_NOEXEC     0x0008  /* 不允许执行程序 */
#define MS_NOATIME    0x0010  /* 不更新 atime */
#define MS_NODIRATIME 0x0020  /* 不更新目录 atime */
#define MS_REMOUNT    0x0040  /* 重新挂载 */
#define MS_BIND       0x0080  /* bind 挂载 */
#define MS_DIRSYNC    0x0100  /* 目录同步写入 */
#define MS_SYNCHRONOUS 0x0200 /* 同步写入 */
#define MS_MANDLOCK   0x0400  /* 强制锁 */

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
    uint32_t d_refcount;       /* dentry 引用计数 */
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
int32_t vfs_mount2(const char *path, uint32_t fs_type, void *data, uint32_t flags);
int32_t vfs_umount(const char *path);
int32_t vfs_remount(const char *path, uint32_t flags);

/* 检查文件系统是否只读 */
int vfs_is_readonly(const char *path);

/* 获取挂载标志 */
uint32_t vfs_get_mount_flags(const char *path);
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
int32_t vfs_fdatasync(file_t *file);
int32_t vfs_syncfs(const char *path);
int32_t vfs_mknod(const char *path, uint32_t mode, uint32_t dev);

/* Directory iteration */
int32_t vfs_opendir(const char *path, file_t **dir);
int32_t vfs_readdir(file_t *dir, vfs_dirent_t *entry);
int32_t vfs_closedir(file_t *dir);

/* Per-process (or global for now) working directory */
int32_t vfs_chdir(const char *path);
const char *vfs_getcwd(void);

/* 文件系统类型信息 */
const char *vfs_fs_type_name(uint32_t fs_type);
int vfs_fs_type_from_name(const char *name, uint32_t *fs_type);

/* 挂载点信息 */
typedef struct {
    char mount_point[256];
    char fs_type[32];
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    uint32_t block_size;
    uint32_t mount_flags;
    uint8_t  read_only;
} vfs_mount_info_t;

int32_t vfs_get_mount_info(const char *path, vfs_mount_info_t *info);
int32_t vfs_list_mounts(vfs_mount_info_t *mounts, uint32_t max_mounts);

/* 错误码辅助 */
const char *vfs_strerror(int32_t err);

#endif
