#include "vfs.h"
#include "vfs_advanced.h"
#include "path.h"
#include "dentry.h"
#include "kheap.h"
#include "sync.h"
#include "string.h"
#include "stddef.h"
#include "ramfs.h"
#include "tarfs.h"
#include "btrfs.h"
#include "xfs.h"
#include "../kernel/permission.h"
#include "../kernel/user.h"
#include "../kernel/klog.h"

dentry_t *root_dentry;
mount_t *mount_list;
static dentry_t *cwd_dentry;
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
extern file_ops_t devfs_file_ops;
extern file_ops_t ramfs_file_ops;
extern file_ops_t tarfs_file_ops;
extern file_ops_t btrfs_file_ops;
extern file_ops_t xfs_file_ops;
extern file_ops_t fuse_file_ops;

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
}

int32_t vfs_mount(const char *path, uint32_t fs_type, void *data) {
    dentry_t *target = NULL;

    spinlock_lock(&vfs_lock);

    if (path[0] == '/' && path[1] == '\0') {
        target = root_dentry;
    } else {
        if (path_resolve(path, &target) != 0) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }
    }

    mount_t *mnt = (mount_t *)kmalloc(sizeof(mount_t));
    if (!mnt) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }
    memset(mnt, 0, sizeof(mount_t));

    superblock_t *sb = (superblock_t *)kmalloc(sizeof(superblock_t));
    if (!sb) {
        kfree(mnt);
        spinlock_unlock(&vfs_lock);
        return -1;
    }
    memset(sb, 0, sizeof(superblock_t));
    sb->fs_type = fs_type;

    int32_t result = -1;
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
        default:
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

        /* Create the file and then resolve it again. */
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

    /* 权限检查: 打开文件需要读取权限 */
    {
        uint32_t proc_uid = user_get_current_uid();
        uint32_t proc_gid = 0;
        user_t *cur = user_get_current();
        if (cur) proc_gid = cur->gid;

        if (perm_check_path(path, PERM_READ) != 0) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }
        if (perm_check_extended(dentry->inode->uid, dentry->inode->gid,
                                (uint16_t)dentry->inode->mode,
                                dentry->inode->acl, proc_uid, proc_gid,
                                PERM_READ) != 0) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }
    }

    file_t *f = (file_t *)kmalloc(sizeof(file_t));
    if (!f) {
        spinlock_unlock(&vfs_lock);
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
    }

    if (f->ops && f->ops->open) {
        int32_t ret = f->ops->open(dentry->inode, f);
        if (ret != 0) {
            kfree(f);
            spinlock_unlock(&vfs_lock);
            return ret;
        }
    }

    *file = f;
    spinlock_unlock(&vfs_lock);
    return 0;
}

int32_t vfs_close(file_t *file) {
    if (!file) return -1;

    spinlock_lock(&vfs_lock);

    file->ref_count--;
    if (file->ops && file->ops->close) {
        file->ops->close(file);
    }

    if (file->ref_count <= 0) {
        kfree(file);
    }

    spinlock_unlock(&vfs_lock);
    return 0;
}

int32_t vfs_read(file_t *file, void *buf, uint32_t count) {
    if (!file || !file->ops || !file->ops->read) return -1;
    if (!(file->flags & FILE_MODE_READ)) return -1;
    return file->ops->read(file, buf, count);
}

int32_t vfs_write(file_t *file, const void *buf, uint32_t count) {
    if (!file || !file->ops || !file->ops->write) return -1;
    if (!(file->flags & FILE_MODE_WRITE)) return -1;

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
            return -1;
        }
    }

    return file->ops->write(file, buf, count);
}

int32_t vfs_seek(file_t *file, int32_t offset, int32_t whence) {
    if (!file || !file->inode) return -1;

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
            return -1;
    }

    if (new_offset < 0) return -1;

    file->offset = (uint32_t)new_offset;
    return (int32_t)file->offset;
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
    /* No-op for ramfs; other filesystems can be flushed here in the future. */
    return 0;
}

int32_t vfs_fsync(file_t *file) {
    if (!file || !file->inode) return -1;

    /* For disk-based filesystems, flush dirty data to disk */
    if (file->inode->sb) {
        if (file->inode->sb->fs_type == FS_TYPE_EXT2) {
            extern int32_t ext2_fsync(uint32_t ino);
            return ext2_fsync(file->inode->ino);
        } else if (file->inode->sb->fs_type == FS_TYPE_EXT4) {
            extern int32_t ext4_fsync(uint32_t ino);
            return ext4_fsync(file->inode->ino);
        } else if (file->inode->sb->fs_type == FS_TYPE_FAT32) {
            /* FAT32: FAT cache is always in sync with disk writes */
            return 0;
        }
    }

    return 0;
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
        return -1;
    }

    dentry_t *parent = NULL;
    if (path_resolve(parent_path, &parent) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    if (!parent->inode || !parent->inode->ops || !parent->inode->ops->symlink) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    int32_t ret = parent->inode->ops->symlink(parent, name, target);
    spinlock_unlock(&vfs_lock);
    return ret;
}

int32_t vfs_readlink(const char *path, char *buf, uint32_t size) {
    dentry_t *dentry = NULL;

    spinlock_lock(&vfs_lock);
    if (path_resolve_nofollow(path, &dentry) != 0 || !dentry->inode) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    if (!dentry->inode->ops || !dentry->inode->ops->readlink) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    int32_t ret = dentry->inode->ops->readlink(dentry, buf, size);
    spinlock_unlock(&vfs_lock);
    return ret;
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

        if (proc_uid != dentry->inode->uid && !is_admin) {
            spinlock_unlock(&vfs_lock);
            return -1;
        }
        uint16_t cur_mode = (uint16_t)dentry->inode->mode;
        if (perm_set_mode(&cur_mode, (uint16_t)mode, proc_uid, is_admin) != 0) {
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
        return -1;
    }

    if (path_parent(newpath, new_parent, PATH_MAX) != 0 ||
        path_basename(newpath, new_name, 256) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    dentry_t *new_dir = NULL;
    if (path_resolve(new_parent, &new_dir) != 0) {
        spinlock_unlock(&vfs_lock);
        return -1;
    }

    /* Try filesystem-specific link operation */
    if (new_dir->inode->ops && new_dir->inode->ops->link) {
        dentry_t *old_dir = old_dentry->parent;
        if (!old_dir) old_dir = root_dentry;
        int32_t ret = new_dir->inode->ops->link(old_dir, old_dentry->name, new_dir, new_name);
        spinlock_unlock(&vfs_lock);
        return ret;
    }

    spinlock_unlock(&vfs_lock);
    return -1;
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
    if (!dir || !dir->inode || !entry) return -1;
    if (!(dir->inode->mode & FILE_MODE_DIR)) return -1;

    if (dir->inode->sb && dir->inode->sb->fs_type == FS_TYPE_RAMFS) {
        /* ramfs fast path */
        ramfs_node_t *parent = (ramfs_node_t *)dir->inode->private_data;
        ramfs_node_t *cur   = (ramfs_node_t *)dir->private_data;
        if (!parent) return -1;
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
        if (!parent) return -1;
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
        if (!parent_d) return -1;
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
