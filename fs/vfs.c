#include "vfs.h"
#include "vfs_advanced.h"
#include "path.h"
#include "dentry.h"
#include "kheap.h"
#include "sync.h"
#include "string.h"
#include "stddef.h"
#include "ramfs.h"
#include "tmpfs.h"
#include "tarfs.h"
#include "btrfs.h"
#include "xfs.h"
#include "../kernel/permission.h"
#include "../kernel/user.h"
#include "../kernel/klog.h"
#include "fs_stat.h"

dentry_t *root_dentry;
mount_t *mount_list;
dentry_t *cwd_dentry;
static char cwd_buf[PATH_MAX];
static spinlock_t vfs_lock;

extern int32_t fat32_mount(superblock_t *sb, void *data);
extern int32_t ext2_mount(superblock_t *sb, void *data);
extern int32_t ext4_mount(superblock_t *sb, void *data);
extern int32_t devfs_mount_internal(superblock_t *sb, void *data);
extern int32_t ramfs_mount(superblock_t *sb, void *data);
extern int32_t btrfs_mount(superblock_t *sb, void *data);
extern int32_t xfs_mount(superblock_t *sb, void *data);
extern int32_t fuse_mount(superblock_t *sb, void *data);
extern int32_t procfs_mount(superblock_t *sb, void *data);
extern int32_t sysfs_mount(superblock_t *sb, void *data);
extern int32_t tmpfs_mount(superblock_t *sb, void *data);
extern file_ops_t devfs_file_ops;
extern file_ops_t ramfs_file_ops;
extern file_ops_t tarfs_file_ops;
extern file_ops_t btrfs_file_ops;
extern file_ops_t xfs_file_ops;
extern file_ops_t fuse_file_ops;
extern file_ops_t tmpfs_file_ops;

void vfs_init(void) {
    spinlock_init(&vfs_lock);
    root_dentry = (dentry_t *)kmalloc(sizeof(dentry_t));
    memset(root_dentry, 0, sizeof(dentry_t));
    root_dentry->name[0] = '/';
    root_dentry->parent = root_dentry;

    inode_t *root_inode = (inode_t *)kmalloc(sizeof(inode_t));
    memset(root_inode, 0, sizeof(inode_t));
    root_inode->ino = 0;
    root_inode->mode = FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_WRITE;
    root_inode->nlinks = 1;
    root_inode->dentries = root_dentry;

    root_dentry->inode = root_inode;
    mount_list = NULL;

    /* Default working directory is the root */
    cwd_dentry = root_dentry;
    cwd_buf[0] = '/';
    cwd_buf[1] = '\0';

    /* Initialize advanced VFS features */
    extern void vfs_advanced_init(void);
    vfs_advanced_init();

    /* Initialize dcache */
    extern void dcache_init(void);
    dcache_init();

    /* Initialize fs sync subsystem */
    extern void fs_sync_init(void);
    fs_sync_init();

    /* Initialize inode cache */
    extern void icache_init(void);
    icache_init();

    /* Initialize page cache */
    extern void page_cache_init(void);
    page_cache_init();

    /* Initialize readahead */
    extern void readahead_init(void);
    readahead_init();

    /* Initialize filesystem statistics */
    fs_stat_init();
}

int32_t vfs_mount(const char *path, uint32_t fs_type, void *data) {
    return vfs_mount2(path, fs_type, data, 0);
}

int32_t vfs_mount2(const char *path, uint32_t fs_type, void *data, uint32_t flags) {
    dentry_t *target = NULL;

    if (!path || path[0] == '\0') {
        return -EINVAL;
    }
    if (fs_type >= FS_TYPE_COUNT) {
        return -ENODEV;
    }

    spinlock_lock(&vfs_lock);

    if (path[0] == '/' && path[1] == '\0') {
        target = root_dentry;
    } else {
        if (path_resolve(path, &target) != 0) {
            spinlock_unlock(&vfs_lock);
            return -ENOENT;
        }
    }

    if (!target->inode) {
        spinlock_unlock(&vfs_lock);
        return -ENOENT;
    }
    if (!(target->inode->mode & FILE_MODE_DIR)) {
        spinlock_unlock(&vfs_lock);
        return -ENOTDIR;
    }
    if (target->mount_point) {
        spinlock_unlock(&vfs_lock);
        return -EBUSY;
    }

    mount_t *mnt = (mount_t *)kmalloc(sizeof(mount_t));
    if (!mnt) {
        spinlock_unlock(&vfs_lock);
        return -ENOMEM;
    }
    memset(mnt, 0, sizeof(mount_t));

    superblock_t *sb = (superblock_t *)kmalloc(sizeof(superblock_t));
    if (!sb) {
        kfree(mnt);
        spinlock_unlock(&vfs_lock);
        return -ENOMEM;
    }
    memset(sb, 0, sizeof(superblock_t));
    sb->fs_type = fs_type;
    sb->block_size = 4096;
    sb->mount_flags = flags;

    int32_t result = -ENODEV;
    switch (fs_type) {
        case FS_TYPE_RAMFS:
            result = ramfs_mount(sb, data);
            break;
        case FS_TYPE_FAT32:
            result = fat32_mount(sb, data);
            break;
        case FS_TYPE_EXT2:
            result = ext2_mount(sb, data);
            break;
        case FS_TYPE_EXT4:
            result = ext4_mount(sb, data);
            break;
        case FS_TYPE_DEVFS:
            result = devfs_mount_internal(sb, data);
            break;
        case FS_TYPE_PROCFS:
            result = procfs_mount(sb, data);
            break;
        case FS_TYPE_SYSFS:
            result = sysfs_mount(sb, data);
            break;
        case FS_TYPE_TMPFS:
            result = tmpfs_mount(sb, data);
            break;
        case FS_TYPE_TARFS:
            result = tarfs_mount(sb, data);
            break;
        case FS_TYPE_BTRFS:
            result = btrfs_mount(sb, data);
            break;
        case FS_TYPE_XFS:
            result = xfs_mount(sb, data);
            break;
        case FS_TYPE_FUSE:
            result = fuse_mount(sb, data);
            break;
        case FS_TYPE_MINIX:
        case FS_TYPE_REISERFS:
        case FS_TYPE_REISER4:
        case FS_TYPE_ZFS:
        case FS_TYPE_UFS:
        case FS_TYPE_JFS:
        case FS_TYPE_HFS:
        case FS_TYPE_HFSPLUS:
        case FS_TYPE_APFS:
        case FS_TYPE_NTFS:
        case FS_TYPE_EXFAT:
        case FS_TYPE_ISO9660:
        case FS_TYPE_UDF:
        case FS_TYPE_SQUASHFS:
        case FS_TYPE_CRAMFS:
        case FS_TYPE_JFFS2:
        case FS_TYPE_YAFFS2:
        case FS_TYPE_UBIFS:
        case FS_TYPE_LOGFS:
        case FS_TYPE_NILFS:
        case FS_TYPE_FFS:
        case FS_TYPE_LUSTRE:
        case FS_TYPE_CEPH:
        case FS_TYPE_GPFS:
        case FS_TYPE_OCFS2:
        case FS_TYPE_GFS2:
        case FS_TYPE_XFS2:
        case FS_TYPE_BFS:
        case FS_TYPE_SYSV:
        case FS_TYPE_COHERENT:
        case FS_TYPE_QNX4:
        case FS_TYPE_QNX6:
        case FS_TYPE_AFFS:
        case FS_TYPE_ADFS:
        case FS_TYPE_HPFS:
        case FS_TYPE_VXFS:
        case FS_TYPE_F2FS:
        case FS_TYPE_ORANGEFS:
        case FS_TYPE_GLUSTERFS:
            klog_warn("vfs_mount: fs type '%s' not yet implemented",
                     vfs_fs_type_name(fs_type));
            result = -ENODEV;
            break;
        default:
            result = -ENODEV;
            break;
    }

    if (result != 0) {
        kfree(sb);
        kfree(mnt);
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    mnt->sb = sb;
    mnt->mount_point = target;
    mnt->root_dentry = sb->root ? sb->root->dentries : target;
    target->mount_point = 1;

    /* 挂载到根路径时，将文件系统 ops 和数据同步到 root_dentry 的 inode */
    if (sb->root && target == root_dentry) {
        /* 设置 sb->root->dentries 并修正 mnt->root_dentry（mount_point 跟踪需要） */
        sb->root->dentries = root_dentry;
        mnt->root_dentry = root_dentry;
        /* 同步关键字段到 root_dentry 的 inode */
        root_dentry->inode->ops = sb->root->ops;
        root_dentry->inode->private_data = sb->root->private_data;
        root_dentry->inode->sb = sb->root->sb;
        root_dentry->inode->ino = sb->root->ino;
        cwd_dentry = root_dentry;
    }

    mnt->next = mount_list;
    mount_list = mnt;

    spinlock_unlock(&vfs_lock);
    fs_stat_mount();
    return 0;
}

int32_t vfs_umount(const char *path) {
    dentry_t *target = NULL;

    spinlock_lock(&vfs_lock);

    if (path_resolve(path, &target) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    mount_t *prev = NULL;
    mount_t *curr = mount_list;
    while (curr) {
        if (curr->mount_point == target) {
            if (prev) {
                prev->next = curr->next;
            } else {
                mount_list = curr->next;
            }
            target->mount_point = 0;
            if (curr->sb) {
                if (curr->sb->fs_data) {
                    kfree(curr->sb->fs_data);
                }
                kfree(curr->sb);
            }
            kfree(curr);
            spinlock_unlock(&vfs_lock);
            fs_stat_umount();
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }

    spinlock_unlock(&vfs_lock);
    return -1;
}

int32_t vfs_open(const char *path, uint32_t flags, file_t **file) {
    dentry_t *dentry = NULL;

    spinlock_lock(&vfs_lock);

    int32_t created = 0;
    if (path_resolve(path, &dentry) != 0) {
        if (!(flags & FILE_MODE_CREATE)) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }

        char parent_path[PATH_MAX];
        char name[256];
        if (path_parent(path, parent_path, PATH_MAX) != 0 ||
            path_basename(path, name, 256) != 0) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }

        dentry_t *parent = NULL;
        if (path_resolve(parent_path, &parent) != 0 ||
            !parent->inode || !parent->inode->ops ||
            !parent->inode->ops->create) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }

        if (parent->inode->ops->create(parent, name,
                                       FILE_MODE_REG | FILE_MODE_READ | FILE_MODE_WRITE) != 0) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }

        if (path_resolve(path, &dentry) != 0 || !dentry->inode) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }
        created = 1;
    }

    if (!dentry->inode) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    /* Permission check: determine required permissions from flags.
     * READ requires read permission; WRITE requires write permission;
     * RDWR requires both.  Sover (uid 0) bypasses all permission checks. */
    {
        uint32_t proc_uid = user_get_current_uid();
        uint32_t proc_gid = 0;
        user_t *cur = user_get_current();
        if (cur) proc_gid = cur->gid;

        uint32_t need_perm = 0;
        if (flags & FILE_MODE_READ)  need_perm |= PERM_READ;
        if (flags & FILE_MODE_WRITE) need_perm |= PERM_WRITE;
        if (need_perm == 0) need_perm = PERM_READ; /* default read */

        if (proc_uid != 0) {
            if (perm_check_path(path, need_perm) != 0) {
                spinlock_unlock(&vfs_lock);
                return -1;
            }
            if (perm_check_extended(dentry->inode->uid, dentry->inode->gid,
                                    (uint16_t)dentry->inode->mode,
                                    dentry->inode->acl, proc_uid, proc_gid,
                                    need_perm) != 0) {
                spinlock_unlock(&vfs_lock);
                return -1;
            }
        }
    }

    spinlock_unlock(&vfs_lock);

    file_t *f = (file_t *)kmalloc(sizeof(file_t));
    if (!f) {
        return -1;
    }
    memset(f, 0, sizeof(file_t));

    f->inode = dentry->inode;
    f->offset = 0;
    f->flags = flags;
    f->ops = NULL;
    f->private_data = NULL;
    f->ref_count = 1;
    (void)created;

    if (dentry->inode->sb && dentry->inode->sb->fs_type == FS_TYPE_DEVFS) {
        extern file_ops_t devfs_file_ops;
        f->ops = &devfs_file_ops;
    } else if (dentry->inode->sb && dentry->inode->sb->fs_type == FS_TYPE_RAMFS) {
        extern file_ops_t ramfs_file_ops;
        f->ops = &ramfs_file_ops;
    } else if (dentry->inode->sb && dentry->inode->sb->fs_type == FS_TYPE_TARFS) {
        extern file_ops_t tarfs_file_ops;
        f->ops = &tarfs_file_ops;
    } else if (dentry->inode->sb && dentry->inode->sb->fs_type == FS_TYPE_FAT32) {
        extern file_ops_t fat32_file_ops;
        f->ops = &fat32_file_ops;
    } else if (dentry->inode->sb && dentry->inode->sb->fs_type == FS_TYPE_EXT2) {
        extern file_ops_t ext2_file_ops;
        f->ops = &ext2_file_ops;
    } else if (dentry->inode->sb && dentry->inode->sb->fs_type == FS_TYPE_EXT4) {
        extern file_ops_t ext4_file_ops;
        f->ops = &ext4_file_ops;
    } else if (dentry->inode->sb && dentry->inode->sb->fs_type == FS_TYPE_BTRFS) {
        f->ops = &btrfs_file_ops;
    } else if (dentry->inode->sb && dentry->inode->sb->fs_type == FS_TYPE_XFS) {
        f->ops = &xfs_file_ops;
    } else if (dentry->inode->sb && dentry->inode->sb->fs_type == FS_TYPE_FUSE) {
        f->ops = &fuse_file_ops;
    } else if (dentry->inode->sb && dentry->inode->sb->fs_type == FS_TYPE_TMPFS) {
        f->ops = &tmpfs_file_ops;
    }

    if (f->ops && f->ops->open) {
        int32_t ret = f->ops->open(dentry->inode, f);
        if (ret != 0) {
            kfree(f);
            return ret;
        }
    }

    *file = f;
    fs_stat_open();
    return 0;
}

int32_t vfs_close(file_t *file) {
    if (!file) return -EBADF;

    spinlock_lock(&vfs_lock);

    file->ref_count--;
    if (file->ops && file->ops->close) {
        file->ops->close(file);
    }

    if (file->ref_count <= 0) {
        kfree(file);
    }

    spinlock_unlock(&vfs_lock);
    fs_stat_close();
    return 0;
}

int32_t vfs_read(file_t *file, void *buf, uint32_t count) {
    if (!file || !buf) return -EINVAL;
    if (!file->ops || !file->ops->read) return -EBADF;
    if (!(file->flags & FILE_MODE_READ)) return -EBADF;

    int32_t ret = file->ops->read(file, buf, count);
    if (ret > 0) {
        fs_stat_read((uint32_t)ret, 0);
    } else if (ret < 0) {
        fs_stat_read(0, 1);
        fs_stat_error(-ret);
    }
    return ret;
}

int32_t vfs_write(file_t *file, const void *buf, uint32_t count) {
    if (!file || !buf) return -EINVAL;
    if (!file->ops || !file->ops->write) return -EBADF;
    if (!(file->flags & FILE_MODE_WRITE)) return -EBADF;

    /* 权限检查: 写入文件需要写入权限 */
    if (file->inode) {
        uint32_t proc_uid = user_get_current_uid();
        uint32_t proc_gid = 0;
        user_t *cur = user_get_current();
        if (cur) proc_gid = cur->gid;

        if (perm_check_extended(file->inode->uid, file->inode->gid,
                                (uint16_t)file->inode->mode,
                                file->inode->acl, proc_uid, proc_gid,
                                PERM_WRITE) != 0) {
            return -EPERM;
        }
    }

    int32_t ret = file->ops->write(file, buf, count);
    if (ret > 0) {
        fs_stat_write((uint32_t)ret, 0);
    } else if (ret < 0) {
        fs_stat_write(0, 1);
        fs_stat_error(-ret);
    }
    return ret;
}

int32_t vfs_seek(file_t *file, int32_t offset, int32_t whence) {
    if (!file || !file->inode) return -EBADF;

    if (file->ops && file->ops->seek) {
        return file->ops->seek(file, offset, whence);
    }

    int32_t new_offset;

    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = (int32_t)file->offset + offset;
            break;
        case SEEK_END:
            new_offset = (int32_t)file->inode->size + offset;
            break;
        default:
            return -EINVAL;
    }

    if (new_offset < 0) return -EINVAL;

    file->offset = (uint32_t)new_offset;
    return (int32_t)file->offset;
}

int32_t vfs_ioctl(file_t *file, uint32_t cmd, void *arg) {
    if (!file) return -EBADF;

    if (file->ops && file->ops->ioctl) {
        return file->ops->ioctl(file, cmd, arg);
    }

    /* Default handling for common ioctls */
    switch (cmd) {
        case FIONREAD:
            if (arg && file->inode) {
                int32_t avail = (int32_t)file->inode->size - (int32_t)file->offset;
                if (avail < 0) avail = 0;
                *(int32_t *)arg = avail;
                return 0;
            }
            return -EINVAL;
        case FIONBIO:
            return 0;
        default:
            break;
    }

    return -ENOSYS;
}

int32_t vfs_mkdir(const char *path, uint32_t mode) {
    char parent_path[PATH_MAX];
    char name[256];

    spinlock_lock(&vfs_lock);

    if (path_parent(path, parent_path, PATH_MAX) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }
    if (path_basename(path, name, 256) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    dentry_t *parent = NULL;
    if (path_resolve(parent_path, &parent) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    if (!parent->inode || !parent->inode->ops || !parent->inode->ops->mkdir) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    /* 权限检查: 创建目录需要父目录写入权限 */
    {
        uint32_t proc_uid = user_get_current_uid();
        uint32_t proc_gid = 0;
        user_t *cur = user_get_current();
        if (cur) proc_gid = cur->gid;

        int pc = perm_check_path(parent_path, PERM_WRITE);
        int pe = perm_check_extended(parent->inode->uid, parent->inode->gid,
                                    (uint16_t)parent->inode->mode,
                                    parent->inode->acl, proc_uid, proc_gid,
                                    PERM_WRITE);
        if (pc != 0 || pe != 0) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }
    }

    int32_t ret = parent->inode->ops->mkdir(parent, name, mode);
    spinlock_unlock(&vfs_lock);
    if (ret == 0) fs_stat_mkdir();
    return ret;
}

int32_t vfs_rmdir(const char *path) {
    char parent_path[PATH_MAX];
    char name[256];

    spinlock_lock(&vfs_lock);

    if (path_parent(path, parent_path, PATH_MAX) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }
    if (path_basename(path, name, 256) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    dentry_t *parent = NULL;
    if (path_resolve(parent_path, &parent) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    if (!parent->inode || !parent->inode->ops || !parent->inode->ops->rmdir) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    /* 权限检查: 删除目录需要父目录写入权限 */
    {
        uint32_t proc_uid = user_get_current_uid();
        uint32_t proc_gid = 0;
        user_t *cur = user_get_current();
        if (cur) proc_gid = cur->gid;

        if (perm_check_path(parent_path, PERM_WRITE) != 0 ||
            perm_check_extended(parent->inode->uid, parent->inode->gid,
                                (uint16_t)parent->inode->mode,
                                parent->inode->acl, proc_uid, proc_gid,
                                PERM_WRITE) != 0) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }
    }

    int32_t ret = parent->inode->ops->rmdir(parent, name);
    spinlock_unlock(&vfs_lock);
    return ret;
}

int32_t vfs_unlink(const char *path) {
    char parent_path[PATH_MAX];
    char name[256];

    spinlock_lock(&vfs_lock);

    if (path_parent(path, parent_path, PATH_MAX) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }
    if (path_basename(path, name, 256) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    dentry_t *parent = NULL;
    if (path_resolve(parent_path, &parent) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    if (!parent->inode || !parent->inode->ops || !parent->inode->ops->unlink) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    /* 权限检查: 删除文件需要父目录写入权限 */
    {
        uint32_t proc_uid = user_get_current_uid();
        uint32_t proc_gid = 0;
        user_t *cur = user_get_current();
        if (cur) proc_gid = cur->gid;

        if (perm_check_path(parent_path, PERM_WRITE) != 0 ||
            perm_check_extended(parent->inode->uid, parent->inode->gid,
                                (uint16_t)parent->inode->mode,
                                parent->inode->acl, proc_uid, proc_gid,
                                PERM_WRITE) != 0) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }
    }

    int32_t ret = parent->inode->ops->unlink(parent, name);
    spinlock_unlock(&vfs_lock);
    if (ret == 0) fs_stat_delete();
    return ret;
}

int32_t vfs_stat(const char *path, inode_t *stat) {
    dentry_t *dentry = NULL;

    spinlock_lock(&vfs_lock);

    if (path_resolve(path, &dentry) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    if (!dentry->inode) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    memcpy(stat, dentry->inode, sizeof(inode_t));
    stat->dentries = NULL;

    spinlock_unlock(&vfs_lock);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Extended VFS API                                                   */
/* ------------------------------------------------------------------ */

int32_t vfs_creat(const char *path, uint32_t mode) {
    char parent_path[PATH_MAX];
    char name[256];
    dentry_t *parent = NULL;

    spinlock_lock(&vfs_lock);

    if (path_parent(path, parent_path, PATH_MAX) != 0 ||
        path_basename(path, name, 256) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }
    if (path_resolve(parent_path, &parent) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }
    if (!parent->inode || !parent->inode->ops || !parent->inode->ops->create) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    int32_t ret = parent->inode->ops->create(parent, name,
                                              mode | FILE_MODE_READ | FILE_MODE_WRITE);
    spinlock_unlock(&vfs_lock);
    return ret;
}

int32_t vfs_truncate(const char *path, uint32_t size) {
    dentry_t *dentry = NULL;
    int32_t ret = -1;

    spinlock_lock(&vfs_lock);
    if (path_resolve(path, &dentry) != 0 || !dentry->inode) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    if (dentry->inode->sb && dentry->inode->sb->fs_type == FS_TYPE_RAMFS) {
        /* ramfs-specific fast path */
        extern int32_t ramfs_file_truncate(inode_t *, uint32_t);
        ret = ramfs_file_truncate(dentry->inode, size);
    } else if (dentry->inode->sb && dentry->inode->sb->fs_type == FS_TYPE_EXT2) {
        /* ext2 truncate */
        extern int32_t ext2_truncate(uint32_t ino, uint32_t new_size);
        ret = ext2_truncate(dentry->inode->ino, size);
        if (ret == 0) dentry->inode->size = size;
    } else if (dentry->inode->ops && (void *)dentry->inode->ops->mkdir) {
        /* Fall back to size 0 + rewrite for filesystems that support it */
        (void)size;
        ret = 0;
    }

    spinlock_unlock(&vfs_lock);
    return ret;
}

int32_t vfs_access(const char *path, uint32_t mode) {
    dentry_t *dentry = NULL;
    int32_t ret = -1;

    spinlock_lock(&vfs_lock);
    if (path_resolve(path, &dentry) != 0 || !dentry->inode) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }
    if ((mode & FILE_MODE_READ)  && !(dentry->inode->mode & FILE_MODE_READ))  ret = -1;
    else if ((mode & FILE_MODE_WRITE) && !(dentry->inode->mode & FILE_MODE_WRITE)) ret = -1;
    else if ((mode & FILE_MODE_EXEC)  && !(dentry->inode->mode & FILE_MODE_EXEC))  ret = -1;
    else ret = 0;
    spinlock_unlock(&vfs_lock);
    return ret;
}

int32_t vfs_sync(void) {
    extern int32_t fs_sync_all(void);
    return fs_sync_all();
}

int32_t vfs_fsync(file_t *file) {
    extern int32_t fs_sync_fsync(file_t *file);
    return fs_sync_fsync(file);
}

int32_t vfs_fdatasync(file_t *file) {
    extern int32_t fs_sync_fdatasync(file_t *file);
    return fs_sync_fdatasync(file);
}

int32_t vfs_syncfs(const char *path) {
    if (!path || !*path) return -EINVAL;

    /* 找到路径对应的 superblock */
    dentry_t *dentry = NULL;
    if (path_resolve(path, &dentry) != 0 || !dentry || !dentry->inode) {
        return -ENOENT;
    }

    if (!dentry->inode->sb) {
        return -ENODEV;
    }

    superblock_t *sb = dentry->inode->sb;
    extern int32_t fs_sync_sb(superblock_t *sb);
    return fs_sync_sb(sb);
}

int32_t vfs_mknod(const char *path, uint32_t mode, uint32_t dev) {
    /* For now, create as a regular file via vfs_creat.
     * Device node support requires devfs integration. */
    (void)dev;

    char parent_path[PATH_MAX];
    char name[256];

    spinlock_lock(&vfs_lock);

    if (path_parent(path, parent_path, PATH_MAX) != 0 ||
        path_basename(path, name, 256) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    dentry_t *parent = NULL;
    if (path_resolve(parent_path, &parent) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    if (!parent->inode || !parent->inode->ops || !parent->inode->ops->create) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    int32_t ret = parent->inode->ops->create(parent, name, mode);
    spinlock_unlock(&vfs_lock);
    return ret;
}

int32_t vfs_rename(const char *old_path, const char *new_path) {
    char old_parent[PATH_MAX], old_name[256];
    char new_parent[PATH_MAX], new_name[256];
    dentry_t *od = NULL, *nd = NULL;

    spinlock_lock(&vfs_lock);
    if (path_parent(old_path, old_parent, PATH_MAX) != 0 ||
        path_basename(old_path, old_name, 256) != 0 ||
        path_parent(new_path, new_parent, PATH_MAX) != 0 ||
        path_basename(new_path, new_name, 256) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }
    if (path_resolve(old_parent, &od) != 0 ||
        path_resolve(new_parent, &nd) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }
    if (!od->inode || !od->inode->ops || !od->inode->ops->rename ||
        !nd->inode || !nd->inode->ops || !nd->inode->ops->rename) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }
    int32_t ret = od->inode->ops->rename(od, old_name, nd, new_name);
    spinlock_unlock(&vfs_lock);
    return ret;
}

int32_t vfs_symlink(const char *target, const char *linkpath) {
    char parent_path[PATH_MAX];
    char name[256];

    spinlock_lock(&vfs_lock);

    if (path_parent(linkpath, parent_path, PATH_MAX) != 0 ||
        path_basename(linkpath, name, 256) != 0) {
        spinlock_unlock(&vfs_lock);
        return -EINVAL;
    }

    dentry_t *parent = NULL;
    if (path_resolve(parent_path, &parent) != 0) {
        spinlock_unlock(&vfs_lock);
        return -ENOENT;
    }

    if (!parent->inode || !parent->inode->ops) {
        spinlock_unlock(&vfs_lock);
        return -ENOSYS;
    }

    if (!parent->inode->ops->symlink) {
        spinlock_unlock(&vfs_lock);
        return -ENOSYS;
    }

    int32_t ret = parent->inode->ops->symlink(parent, name, target);
    spinlock_unlock(&vfs_lock);
    return ret < 0 ? -ENOSYS : ret;
}

int32_t vfs_readlink(const char *path, char *buf, uint32_t size) {
    dentry_t *dentry = NULL;

    spinlock_lock(&vfs_lock);
    if (path_resolve_nofollow(path, &dentry) != 0 || !dentry->inode) {
        spinlock_unlock(&vfs_lock);
        return -ENOENT;
    }

    if (!dentry->inode->ops || !dentry->inode->ops->readlink) {
        spinlock_unlock(&vfs_lock);
        return -ENOSYS;
    }

    int32_t ret = dentry->inode->ops->readlink(dentry, buf, size);
    spinlock_unlock(&vfs_lock);
    return ret < 0 ? -ENOSYS : ret;
}

int32_t vfs_chmod(const char *path, uint32_t mode) {
    dentry_t *dentry = NULL;

    spinlock_lock(&vfs_lock);
    if (path_resolve(path, &dentry) != 0 || !dentry->inode) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    /* 权限控制: 仅 owner 或 admin 可更改文件模式 */
    {
        uint32_t proc_uid = user_get_current_uid();
        int is_admin = perm_is_admin();
        uint32_t file_uid = dentry->inode->uid;

        uint16_t cur_mode = (uint16_t)dentry->inode->mode;
        if (perm_set_mode(&cur_mode, (uint16_t)mode, file_uid, proc_uid, is_admin) != 0) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }
        /* Update mode: preserve file type bits, replace permission bits */
        dentry->inode->mode = (dentry->inode->mode & (FILE_MODE_DIR | FILE_MODE_REG | FILE_MODE_LNK)) |
                              (cur_mode & (FILE_MODE_READ | FILE_MODE_WRITE | FILE_MODE_EXEC));
    }

    /* For disk-based filesystems, write back to disk */
    if (dentry->inode->sb) {
        if (dentry->inode->sb->fs_type == FS_TYPE_EXT2) {
            extern int32_t ext2_chmod(uint32_t ino, uint32_t mode);
            ext2_chmod(dentry->inode->ino, mode);
        } else if (dentry->inode->sb->fs_type == FS_TYPE_EXT4) {
            extern int32_t ext4_chmod(uint32_t ino, uint32_t mode);
            ext4_chmod(dentry->inode->ino, mode);
        }
    }

    spinlock_unlock(&vfs_lock);
    return 0;
}

int32_t vfs_chown(const char *path, uint32_t uid, uint32_t gid) {
    dentry_t *dentry = NULL;

    spinlock_lock(&vfs_lock);
    if (path_resolve(path, &dentry) != 0 || !dentry->inode) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    dentry->inode->uid = uid;
    dentry->inode->gid = gid;

    /* For disk-based filesystems, write back to disk */
    if (dentry->inode->sb) {
        if (dentry->inode->sb->fs_type == FS_TYPE_EXT2) {
            extern int32_t ext2_chown(uint32_t ino, uint32_t uid, uint32_t gid);
            ext2_chown(dentry->inode->ino, uid, gid);
        } else if (dentry->inode->sb->fs_type == FS_TYPE_EXT4) {
            extern int32_t ext4_chown(uint32_t ino, uint32_t uid, uint32_t gid);
            ext4_chown(dentry->inode->ino, uid, gid);
        }
    }

    spinlock_unlock(&vfs_lock);
    return 0;
}

int32_t vfs_link(const char *oldpath, const char *newpath) {
    char new_parent[PATH_MAX];
    char new_name[256];

    spinlock_lock(&vfs_lock);

    dentry_t *old_dentry = NULL;
    if (path_resolve(oldpath, &old_dentry) != 0 || !old_dentry->inode) {
        spinlock_unlock(&vfs_lock);
        return -ENOENT;
    }

    if (path_parent(newpath, new_parent, PATH_MAX) != 0 ||
        path_basename(newpath, new_name, 256) != 0) {
        spinlock_unlock(&vfs_lock);
        return -EINVAL;
    }

    dentry_t *new_dir = NULL;
    if (path_resolve(new_parent, &new_dir) != 0) {
        spinlock_unlock(&vfs_lock);
        return -ENOENT;
    }

    if (!new_dir->inode || !new_dir->inode->ops) {
        spinlock_unlock(&vfs_lock);
        return -ENOSYS;
    }

    /* Try filesystem-specific link operation */
    if (new_dir->inode->ops->link) {
        dentry_t *old_dir = old_dentry->parent;
        if (!old_dir) old_dir = root_dentry;
        int32_t ret = new_dir->inode->ops->link(old_dir, old_dentry->name, new_dir, new_name);
        spinlock_unlock(&vfs_lock);
        return ret < 0 ? -ENOSYS : ret;
    }

    spinlock_unlock(&vfs_lock);
    return -ENOSYS;
}

int32_t vfs_utimes(const char *path, uint32_t atime, uint32_t mtime) {
    dentry_t *dentry = NULL;

    spinlock_lock(&vfs_lock);
    if (path_resolve(path, &dentry) != 0 || !dentry->inode) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    dentry->inode->atime = atime;
    dentry->inode->mtime = mtime;

    /* For disk-based filesystems, write back to disk */
    if (dentry->inode->sb) {
        if (dentry->inode->sb->fs_type == FS_TYPE_EXT2) {
            extern int32_t ext2_utimes(uint32_t ino, uint32_t atime, uint32_t mtime);
            ext2_utimes(dentry->inode->ino, atime, mtime);
        } else if (dentry->inode->sb->fs_type == FS_TYPE_EXT4) {
            extern int32_t ext4_utimes(uint32_t ino, uint32_t atime, uint32_t mtime);
            ext4_utimes(dentry->inode->ino, atime, mtime);
        }
    }

    spinlock_unlock(&vfs_lock);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Directory iteration                                                */
/* ------------------------------------------------------------------ */

int32_t vfs_opendir(const char *path, file_t **dir) {
    return vfs_open(path, FILE_MODE_READ, dir);
}

int32_t vfs_closedir(file_t *dir) {
    return vfs_close(dir);
}

/* Sequential readdir: file->private_data carries the iterator state. */
int32_t vfs_readdir(file_t *dir, vfs_dirent_t *entry) {
    if (!dir || !dir->inode || !entry) return -EBADF;
    if (!(dir->inode->mode & FILE_MODE_DIR)) return -ENOTDIR;

    if (dir->inode->sb && dir->inode->sb->fs_type == FS_TYPE_RAMFS) {
        /* ramfs fast path */
        ramfs_node_t *parent = (ramfs_node_t *)dir->inode->private_data;
        ramfs_node_t *cur   = (ramfs_node_t *)dir->private_data;
        if (!parent) return -ENOENT;
        if (!cur) {
            cur = parent->child;
        } else {
            cur = cur->next_sibling;
        }
        if (!cur) {
            /* end of directory - reset iterator for next opendir */
            dir->private_data = NULL;
            return 0;
        }
        dir->private_data = cur;
        entry->ino  = cur->ino;
        entry->off  = 0;
        entry->reclen = sizeof(vfs_dirent_t);
        entry->type = (cur->mode & FILE_MODE_DIR) ? DT_DIR :
                      (cur->mode & FILE_MODE_LNK) ? DT_LNK : DT_REG;
        strncpy(entry->name, cur->name, 255);
        entry->name[255] = '\0';
        return 1;
    }

    if (dir->inode->sb && dir->inode->sb->fs_type == FS_TYPE_TARFS) {
        /* tarfs path - walk tarfs_node children */
        tarfs_node_t *parent = (tarfs_node_t *)dir->inode->private_data;
        tarfs_node_t *cur    = (tarfs_node_t *)dir->private_data;
        if (!parent) return -ENOENT;
        if (!cur) {
            cur = parent->child;
        } else {
            cur = cur->next_sibling;
        }
        if (!cur) {
            dir->private_data = NULL;
            return 0;
        }
        dir->private_data = cur;
        entry->ino  = cur->ino;
        entry->off  = 0;
        entry->reclen = sizeof(vfs_dirent_t);
        entry->type = (cur->mode & FILE_MODE_DIR) ? DT_DIR :
                      (cur->mode & FILE_MODE_LNK) ? DT_LNK : DT_REG;
        strncpy(entry->name, cur->name, 255);
        entry->name[255] = '\0';
        return 1;
    }

    if (dir->inode->sb && dir->inode->sb->fs_type == FS_TYPE_DEVFS) {
        /* devfs path - walk dentry children (devfs creates dentries at registration) */
        extern dentry_t *devfs_root_dentry;
        dentry_t *parent_d = dir->inode->dentries;
        if (!parent_d) parent_d = devfs_root_dentry;
        dentry_t *cur = (dentry_t *)dir->private_data;
        if (!parent_d) return -ENOENT;
        if (!cur) {
            cur = parent_d->child;
        } else {
            cur = cur->next_sibling;
        }
        if (!cur) {
            dir->private_data = NULL;
            return 0;
        }
        dir->private_data = cur;
        entry->ino  = cur->inode ? cur->inode->ino : 0;
        entry->off  = 0;
        entry->reclen = sizeof(vfs_dirent_t);
        entry->type = (cur->inode && (cur->inode->mode & FILE_MODE_DIR)) ? DT_DIR :
                      (cur->inode && (cur->inode->mode & FILE_MODE_LNK)) ? DT_LNK : DT_REG;
        strncpy(entry->name, cur->name, 255);
        entry->name[255] = '\0';
        return 1;
    }

    if (dir->inode->sb && dir->inode->sb->fs_type == FS_TYPE_TMPFS) {
        tmpfs_node_t *parent = (tmpfs_node_t *)dir->inode->private_data;
        tmpfs_node_t *cur   = (tmpfs_node_t *)dir->private_data;
        if (!parent) return -ENOENT;
        if (!cur) {
            cur = parent->child;
        } else {
            cur = cur->next_sibling;
        }
        if (!cur) {
            dir->private_data = NULL;
            return 0;
        }
        dir->private_data = cur;
        entry->ino  = cur->ino;
        entry->off  = 0;
        entry->reclen = sizeof(vfs_dirent_t);
        entry->type = (cur->mode & FILE_MODE_DIR) ? DT_DIR :
                      (cur->mode & FILE_MODE_LNK) ? DT_LNK : DT_REG;
        strncpy(entry->name, cur->name, 255);
        entry->name[255] = '\0';
        return 1;
    }

    /* Generic fallback: walk the dentry children list using
     * private_data as the iterator (same pattern as ramfs/tarfs). */
    {
        dentry_t *parent_d = dir->inode->dentries;
        dentry_t *cur = (dentry_t *)dir->private_data;
        if (!parent_d) return -ENOENT;
        if (!cur) {
            cur = parent_d->child;
        } else {
            cur = cur->next_sibling;
        }
        if (!cur) {
            dir->private_data = NULL;
            return 0;
        }
        dir->private_data = cur;
        entry->ino  = cur->inode ? cur->inode->ino : 0;
        entry->off  = 0;
        entry->reclen = sizeof(vfs_dirent_t);
        entry->type = (cur->inode && (cur->inode->mode & FILE_MODE_DIR)) ? DT_DIR :
                      (cur->inode && (cur->inode->mode & FILE_MODE_LNK)) ? DT_LNK : DT_REG;
        strncpy(entry->name, cur->name, 255);
        entry->name[255] = '\0';
        return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Working directory                                                   */
/* ------------------------------------------------------------------ */

int32_t vfs_chdir(const char *path) {
    dentry_t *target = NULL;
    int32_t ret = -1;

    spinlock_lock(&vfs_lock);
    if (path_resolve(path, &target) != 0) goto out;
    if (!target->inode || !(target->inode->mode & FILE_MODE_DIR)) goto out;
    cwd_dentry = target;
    /* Build the canonical path string. */
    if (target == root_dentry || strcmp(target->name, "/") == 0) {
        cwd_buf[0] = '/';
        cwd_buf[1] = '\0';
    } else {
        uint32_t pos = PATH_MAX - 1;
        cwd_buf[pos] = '\0';
        dentry_t *p = target;
        while (p && p != root_dentry && p->parent != p) {
            uint32_t nlen = 0;
            while (p->name[nlen]) nlen++;
            if (pos < nlen + 1) { ret = -1; goto out; }
            pos -= nlen;
            memcpy(cwd_buf + pos, p->name, nlen);
            if (pos > 0) {
                pos--;
                cwd_buf[pos] = '/';
            }
            p = p->parent;
        }
        if (pos >= PATH_MAX) { ret = -1; goto out; }
        memmove(cwd_buf, cwd_buf + pos, strlen(cwd_buf + pos) + 1);
    }
    ret = 0;
out:
    spinlock_unlock(&vfs_lock);
    return ret;
}

const char *vfs_getcwd(void) {
    return cwd_buf;
}

/* ===== 文件系统类型名称映射 ===== */

static const char *fs_type_names[] = {
    [FS_TYPE_RAMFS] = "ramfs",
    [FS_TYPE_FAT32] = "vfat",
    [FS_TYPE_EXT2] = "ext2",
    [FS_TYPE_DEVFS] = "devtmpfs",
    [FS_TYPE_EXT4] = "ext4",
    [FS_TYPE_PROCFS] = "proc",
    [FS_TYPE_SYSFS] = "sysfs",
    [FS_TYPE_BTRFS] = "btrfs",
    [FS_TYPE_XFS] = "xfs",
    [FS_TYPE_TARFS] = "tarfs",
    [FS_TYPE_FUSE] = "fuse",
    [FS_TYPE_MINIX] = "minix",
    [FS_TYPE_REISERFS] = "reiserfs",
    [FS_TYPE_REISER4] = "reiser4",
    [FS_TYPE_ZFS] = "zfs",
    [FS_TYPE_UFS] = "ufs",
    [FS_TYPE_JFS] = "jfs",
    [FS_TYPE_HFS] = "hfs",
    [FS_TYPE_HFSPLUS] = "hfsplus",
    [FS_TYPE_APFS] = "apfs",
    [FS_TYPE_NTFS] = "ntfs",
    [FS_TYPE_EXFAT] = "exfat",
    [FS_TYPE_ISO9660] = "iso9660",
    [FS_TYPE_UDF] = "udf",
    [FS_TYPE_SQUASHFS] = "squashfs",
    [FS_TYPE_CRAMFS] = "cramfs",
    [FS_TYPE_JFFS2] = "jffs2",
    [FS_TYPE_YAFFS2] = "yaffs2",
    [FS_TYPE_UBIFS] = "ubifs",
    [FS_TYPE_LOGFS] = "logfs",
    [FS_TYPE_NILFS] = "nilfs",
    [FS_TYPE_FFS] = "ffs",
    [FS_TYPE_LUSTRE] = "lustre",
    [FS_TYPE_CEPH] = "ceph",
    [FS_TYPE_GPFS] = "gpfs",
    [FS_TYPE_OCFS2] = "ocfs2",
    [FS_TYPE_GFS2] = "gfs2",
    [FS_TYPE_XFS2] = "xfs2",
    [FS_TYPE_BFS] = "bfs",
    [FS_TYPE_SYSV] = "sysv",
    [FS_TYPE_COHERENT] = "coherent",
    [FS_TYPE_QNX4] = "qnx4",
    [FS_TYPE_QNX6] = "qnx6",
    [FS_TYPE_AFFS] = "affs",
    [FS_TYPE_ADFS] = "adfs",
    [FS_TYPE_HPFS] = "hpfs",
    [FS_TYPE_VXFS] = "vxfs",
    [FS_TYPE_F2FS] = "f2fs",
    [FS_TYPE_ORANGEFS] = "orangefs",
    [FS_TYPE_GLUSTERFS] = "glusterfs",
};

const char *vfs_fs_type_name(uint32_t fs_type) {
    if (fs_type >= FS_TYPE_COUNT) return "unknown";
    const char *name = fs_type_names[fs_type];
    return name ? name : "unknown";
}

int vfs_fs_type_from_name(const char *name, uint32_t *fs_type) {
    if (!name || !fs_type) return -EINVAL;
    for (uint32_t i = 0; i < FS_TYPE_COUNT; i++) {
        if (fs_type_names[i] && strcmp(fs_type_names[i], name) == 0) {
            *fs_type = i;
            return 0;
        }
    }
    return -ENODEV;
}

/* ===== 挂载点信息查询 ===== */

static void vfs_dentry_path(dentry_t *dentry, char *buf, int bufsize) {
    char tmp[PATH_MAX];
    int len = 0;
    tmp[0] = '\0';

    dentry_t *cur = dentry;
    while (cur && cur != root_dentry && cur != cur->parent) {
        int nlen = (int)strlen(cur->name);
        if (len + nlen + 1 >= PATH_MAX) break;
        memmove(tmp + nlen + 1, tmp, len);
        tmp[0] = '/';
        memcpy(tmp + 1, cur->name, nlen);
        len += nlen + 1;
        cur = cur->parent;
    }

    if (len == 0) {
        tmp[0] = '/';
        tmp[1] = '\0';
        len = 1;
    }

    strncpy(buf, tmp, bufsize - 1);
    buf[bufsize - 1] = '\0';
}

int32_t vfs_get_mount_info(const char *path, vfs_mount_info_t *info) {
    if (!path || !info) return -EINVAL;

    dentry_t *target = NULL;
    spinlock_lock(&vfs_lock);

    if (path_resolve(path, &target) != 0) {
        spinlock_unlock(&vfs_lock);
        return -ENOENT;
    }

    mount_t *mnt = mount_list;
    while (mnt) {
        if (mnt->mount_point == target ||
            (target->mount_point && mnt->root_dentry == target)) {
            memset(info, 0, sizeof(vfs_mount_info_t));
            vfs_dentry_path(mnt->mount_point, info->mount_point, sizeof(info->mount_point));
            strncpy(info->fs_type, vfs_fs_type_name(mnt->sb->fs_type), sizeof(info->fs_type) - 1);
            info->total_blocks = mnt->sb->total_blocks;
            info->free_blocks = mnt->sb->free_blocks;
            info->block_size = mnt->sb->block_size;
            info->read_only = 0;
            spinlock_unlock(&vfs_lock);
            return 0;
        }
        mnt = mnt->next;
    }

    spinlock_unlock(&vfs_lock);
    return -ENOENT;
}

int32_t vfs_list_mounts(vfs_mount_info_t *mounts, uint32_t max_mounts) {
    if (!mounts || max_mounts == 0) return -EINVAL;

    spinlock_lock(&vfs_lock);
    uint32_t count = 0;
    mount_t *mnt = mount_list;

    while (mnt && count < max_mounts) {
        memset(&mounts[count], 0, sizeof(vfs_mount_info_t));
        vfs_dentry_path(mnt->mount_point, mounts[count].mount_point, sizeof(mounts[count].mount_point));
        strncpy(mounts[count].fs_type, vfs_fs_type_name(mnt->sb->fs_type), sizeof(mounts[count].fs_type) - 1);
        mounts[count].total_blocks = mnt->sb->total_blocks;
        mounts[count].free_blocks = mnt->sb->free_blocks;
        mounts[count].total_inodes = mnt->sb->total_inodes;
        mounts[count].free_inodes = mnt->sb->free_inodes;
        mounts[count].block_size = mnt->sb->block_size;
        mounts[count].mount_flags = mnt->sb->mount_flags;
        mounts[count].read_only = (mnt->sb->mount_flags & MS_RDONLY) ? 1 : 0;
        count++;
        mnt = mnt->next;
    }

    spinlock_unlock(&vfs_lock);
    return (int32_t)count;
}

/* ---- 辅助：查找路径对应的挂载点 ---- */
static mount_t *find_mount_by_path_locked(const char *path) {
    dentry_t *dentry = NULL;
    if (path_resolve(path, &dentry) != 0 || !dentry) {
        return NULL;
    }

    /* 向上查找最近的挂载点 */
    mount_t *best = NULL;
    mount_t *mnt = mount_list;
    while (mnt) {
        if (mnt->mount_point == dentry) {
            return mnt;  /* 精确匹配 */
        }
        /* 检查是否是祖先挂载点 */
        dentry_t *d = dentry;
        while (d && d != d->parent) {
            if (d == mnt->mount_point) {
                best = mnt;
                break;
            }
            d = d->parent;
        }
        mnt = mnt->next;
    }
    return best;
}

/* ---- 重新挂载 ---- */
int32_t vfs_remount(const char *path, uint32_t flags) {
    if (!path || !*path) return -EINVAL;

    spinlock_lock(&vfs_lock);

    mount_t *mnt = find_mount_by_path_locked(path);
    if (!mnt) {
        spinlock_unlock(&vfs_lock);
        return -ENOENT;
    }

    mnt->sb->mount_flags = flags;

    spinlock_unlock(&vfs_lock);
    fs_stat_umount();  /* 统计 */
    fs_stat_mount();
    return 0;
}

/* ---- 检查是否只读 ---- */
int vfs_is_readonly(const char *path) {
    if (!path || !*path) return 0;

    spinlock_lock(&vfs_lock);

    mount_t *mnt = find_mount_by_path_locked(path);
    int readonly = mnt && (mnt->sb->mount_flags & MS_RDONLY);

    spinlock_unlock(&vfs_lock);
    return readonly ? 1 : 0;
}

/* ---- 获取挂载标志 ---- */
uint32_t vfs_get_mount_flags(const char *path) {
    if (!path || !*path) return 0;

    spinlock_lock(&vfs_lock);

    mount_t *mnt = find_mount_by_path_locked(path);
    uint32_t flags = mnt ? mnt->sb->mount_flags : 0;

    spinlock_unlock(&vfs_lock);
    return flags;
}

/* ===== 错误码字符串 ===== */

const char *vfs_strerror(int32_t err) {
    switch (err) {
        case 0: return "Success";
        case -EPERM: return "Operation not permitted";
        case -ENOENT: return "No such file or directory";
        case -EIO: return "I/O error";
        case -EBADF: return "Bad file descriptor";
        case -ENOMEM: return "Out of memory";
        case -EBUSY: return "Device or resource busy";
        case -EEXIST: return "File exists";
        case -ENODEV: return "No such device";
        case -ENOTDIR: return "Not a directory";
        case -EISDIR: return "Is a directory";
        case -EINVAL: return "Invalid argument";
        case -ENOTEMPTY: return "Directory not empty";
        case -ENOSPC: return "No space left on device";
        case -EROFS: return "Read-only file system";
        case -ENOSYS: return "Function not implemented";
        default: return "Unknown error";
    }
}
