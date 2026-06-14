/*
 * fuse.c - FUSE (Filesystem in Userspace) 框架实现
 *
 * 提供内核侧 FUSE 支持，将 VFS 操作转换为 FUSE 请求，
 * 通过管道或共享内存与用户态守护进程通信。
 * 当前为框架实现，用户态通信使用简化方式。
 */

#include "fuse.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "spinlock.h"

/* ---- 全局状态 ---- */
static fuse_mount_t *fuse_mount_list = NULL;
static spinlock_t fuse_lock;
static uint32_t fuse_global_unique = 1;

/* ---- FUSE 文件操作 ---- */

static int32_t fuse_file_open(inode_t *inode, file_t *file) {
    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_OPEN;
    req.nodeid = inode->ino;
    req.data_len = 0;

    if (fuse_send_request(&req, &resp) != 0 || resp.error != 0) {
        return -1;
    }

    /* 保存守护进程返回的文件句柄 */
    file->private_data = (void *)(uintptr_t)resp.data[0];
    return 0;
}

static int32_t fuse_file_read(file_t *file, void *buf, uint32_t count) {
    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_READ;
    req.nodeid = file->inode ? file->inode->ino : 0;
    req.data_len = sizeof(uint32_t) * 3;

    /* 在 data 中编码: offset, size, fh */
    uint32_t *args = (uint32_t *)req.data;
    args[0] = file->offset;
    args[1] = count;
    args[2] = (uint32_t)(uintptr_t)file->private_data;

    if (fuse_send_request(&req, &resp) != 0 || resp.error != 0) {
        return resp.error ? resp.error : -1;
    }

    /* 从响应中复制数据 */
    uint32_t to_copy = resp.data_len;
    if (to_copy > count) to_copy = count;
    memcpy(buf, resp.data, to_copy);
    file->offset += to_copy;

    return (int32_t)to_copy;
}

static int32_t fuse_file_write(file_t *file, const void *buf, uint32_t count) {
    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_WRITE;
    req.nodeid = file->inode ? file->inode->ino : 0;
    req.data_len = sizeof(uint32_t) * 3 + count;

    /* 在 data 中编码: offset, size, fh, 然后是实际数据 */
    uint32_t *args = (uint32_t *)req.data;
    args[0] = file->offset;
    args[1] = count;
    args[2] = (uint32_t)(uintptr_t)file->private_data;
    if (count + sizeof(uint32_t) * 3 <= sizeof(req.data)) {
        memcpy(req.data + sizeof(uint32_t) * 3, buf, count);
    }

    if (fuse_send_request(&req, &resp) != 0 || resp.error != 0) {
        return resp.error ? resp.error : -1;
    }

    uint32_t written = *(uint32_t *)resp.data;
    file->offset += written;
    return (int32_t)written;
}

static int32_t fuse_file_close(file_t *file) {
    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_RELEASE;
    req.nodeid = file->inode ? file->inode->ino : 0;
    req.data_len = sizeof(uint32_t);
    uint32_t *args = (uint32_t *)req.data;
    args[0] = (uint32_t)(uintptr_t)file->private_data;

    fuse_send_request(&req, &resp);
    return 0;
}

static int32_t fuse_file_ioctl(file_t *file, uint32_t cmd, void *arg) {
    (void)file;
    (void)cmd;
    (void)arg;
    return -1;
}

static int32_t fuse_file_seek(file_t *file, int32_t offset, int32_t whence) {
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
    return new_offset;
}

/* ---- FUSE inode 操作 ---- */

static dentry_t *fuse_inode_lookup(dentry_t *dir, const char *name) {
    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_LOOKUP;
    req.nodeid = dir->inode ? dir->inode->ino : 0;
    req.data_len = strlen(name) + 1;
    strncpy(req.data, name, sizeof(req.data) - 1);

    if (fuse_send_request(&req, &resp) != 0 || resp.error != 0) {
        return NULL;
    }

    /* 创建新的 dentry 和 inode */
    dentry_t *d = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!d) return NULL;
    memset(d, 0, sizeof(dentry_t));
    strncpy(d->name, name, 255);

    inode_t *ino = (inode_t *)kmalloc(sizeof(inode_t));
    if (!ino) {
        kfree(d);
        return NULL;
    }
    memset(ino, 0, sizeof(inode_t));

    /* 从响应中解析 inode 属性 */
    uint32_t *attrs = (uint32_t *)resp.data;
    ino->ino = attrs[0];
    ino->mode = attrs[1];
    ino->size = attrs[2];
    ino->nlinks = 1;

    d->inode = ino;
    return d;
}

static int32_t fuse_inode_create(dentry_t *dir, const char *name, uint32_t mode) {
    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_MKNOD;
    req.nodeid = dir->inode ? dir->inode->ino : 0;
    req.data_len = sizeof(uint32_t) + strlen(name) + 1;
    uint32_t *mode_ptr = (uint32_t *)req.data;
    *mode_ptr = mode;
    strncpy(req.data + sizeof(uint32_t), name, sizeof(req.data) - sizeof(uint32_t) - 1);

    if (fuse_send_request(&req, &resp) != 0) return -1;
    return resp.error;
}

static int32_t fuse_inode_mkdir(dentry_t *dir, const char *name, uint32_t mode) {
    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_MKDIR;
    req.nodeid = dir->inode ? dir->inode->ino : 0;
    req.data_len = sizeof(uint32_t) + strlen(name) + 1;
    uint32_t *mode_ptr = (uint32_t *)req.data;
    *mode_ptr = mode;
    strncpy(req.data + sizeof(uint32_t), name, sizeof(req.data) - sizeof(uint32_t) - 1);

    if (fuse_send_request(&req, &resp) != 0) return -1;
    return resp.error;
}

static int32_t fuse_inode_unlink(dentry_t *dir, const char *name) {
    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_UNLINK;
    req.nodeid = dir->inode ? dir->inode->ino : 0;
    req.data_len = strlen(name) + 1;
    strncpy(req.data, name, sizeof(req.data) - 1);

    if (fuse_send_request(&req, &resp) != 0) return -1;
    return resp.error;
}

static int32_t fuse_inode_rmdir(dentry_t *dir, const char *name) {
    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_RMDIR;
    req.nodeid = dir->inode ? dir->inode->ino : 0;
    req.data_len = strlen(name) + 1;
    strncpy(req.data, name, sizeof(req.data) - 1);

    if (fuse_send_request(&req, &resp) != 0) return -1;
    return resp.error;
}

static int32_t fuse_inode_rename(dentry_t *old_dir, const char *old_name,
                                  dentry_t *new_dir, const char *new_name) {
    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_RENAME;
    req.nodeid = old_dir->inode ? old_dir->inode->ino : 0;
    req.data_len = sizeof(uint64_t) + strlen(old_name) + 1 + strlen(new_name) + 1;
    uint64_t *new_dir_id = (uint64_t *)req.data;
    *new_dir_id = new_dir->inode ? new_dir->inode->ino : 0;
    uint32_t off = sizeof(uint64_t);
    strncpy(req.data + off, old_name, sizeof(req.data) - off - 1);
    off += strlen(old_name) + 1;
    if (off < sizeof(req.data)) {
        strncpy(req.data + off, new_name, sizeof(req.data) - off - 1);
    }

    if (fuse_send_request(&req, &resp) != 0) return -1;
    return resp.error;
}

static int32_t fuse_inode_readlink(dentry_t *dentry, char *buf, uint32_t size) {
    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_READLINK;
    req.nodeid = dentry->inode ? dentry->inode->ino : 0;
    req.data_len = 0;

    if (fuse_send_request(&req, &resp) != 0 || resp.error != 0) {
        return -1;
    }

    uint32_t to_copy = resp.data_len;
    if (to_copy > size - 1) to_copy = size - 1;
    memcpy(buf, resp.data, to_copy);
    buf[to_copy] = '\0';
    return (int32_t)to_copy;
}

static int32_t fuse_inode_symlink(dentry_t *dir, const char *name, const char *target) {
    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_SYMLINK;
    req.nodeid = dir->inode ? dir->inode->ino : 0;
    req.data_len = strlen(name) + 1 + strlen(target) + 1;
    strncpy(req.data, name, sizeof(req.data) - 1);
    uint32_t off = strlen(name) + 1;
    if (off < sizeof(req.data)) {
        strncpy(req.data + off, target, sizeof(req.data) - off - 1);
    }

    if (fuse_send_request(&req, &resp) != 0) return -1;
    return resp.error;
}

/* ---- FUSE 操作表 ---- */

file_ops_t fuse_file_ops = {
    .open   = fuse_file_open,
    .read   = fuse_file_read,
    .write  = fuse_file_write,
    .close  = fuse_file_close,
    .seek   = fuse_file_seek,
    .ioctl  = fuse_file_ioctl,
};

static inode_ops_t fuse_inode_ops = {
    .lookup   = fuse_inode_lookup,
    .create   = fuse_inode_create,
    .mkdir    = fuse_inode_mkdir,
    .unlink   = fuse_inode_unlink,
    .rmdir    = fuse_inode_rmdir,
    .rename   = fuse_inode_rename,
    .readlink = fuse_inode_readlink,
    .symlink  = fuse_inode_symlink,
};

/* ---- FUSE 守护进程通信 ---- */

int fuse_send_request(fuse_request_t *req, fuse_response_t *resp) {
    /* 简化实现: 直接在内核中模拟响应
     * 在完整实现中，这里应该:
     * 1. 将请求写入共享内存或管道
     * 2. 通知用户态守护进程 (通过信号或事件)
     * 3. 等待守护进程的响应
     * 4. 从共享内存或管道读取响应 */

    if (!req || !resp) return -1;

    /* 初始化响应为默认值 */
    memset(resp, 0, sizeof(fuse_response_t));
    resp->unique = req->unique;

    /* FUSE_INIT 特殊处理: 返回协议版本 */
    if (req->opcode == FUSE_INIT) {
        resp->error = 0;
        uint32_t *ver = (uint32_t *)resp->data;
        ver[0] = 7;   /* 主版本号 */
        ver[1] = 23;  /* 次版本号 */
        resp->data_len = sizeof(uint32_t) * 2;
        return 0;
    }

    /* 其他操作: 返回 -ENOSYS (函数未实现)
     * 在实际实现中，这里会与守护进程通信 */
    resp->error = -38;  /* -ENOSYS */
    resp->data_len = 0;

    return 0;
}

/* ---- FUSE 挂载 ---- */

int fuse_mount(superblock_t *sb, void *data) {
    if (!sb) return -1;

    /* 设置超级块 */
    sb->fs_type = FS_TYPE_FUSE;
    sb->block_size = 4096;
    sb->fs_data = data;

    /* 创建根 inode */
    inode_t *root = (inode_t *)kmalloc(sizeof(inode_t));
    if (!root) return -1;
    memset(root, 0, sizeof(inode_t));
    root->ino = 1;  /* FUSE 根节点 ID 为 1 */
    root->mode = FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_WRITE;
    root->ops = &fuse_inode_ops;
    root->sb = sb;

    /* 创建根 dentry */
    dentry_t *root_d = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!root_d) {
        kfree(root);
        return -1;
    }
    memset(root_d, 0, sizeof(dentry_t));
    root_d->inode = root;
    root_d->name[0] = '/';
    root->dentries = root_d;

    sb->root = root;

    /* 发送 FUSE_INIT 请求 */
    fuse_request_t req;
    fuse_response_t resp;
    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_INIT;
    req.data_len = sizeof(uint32_t) * 2;
    uint32_t *ver = (uint32_t *)req.data;
    ver[0] = 7;   /* 请求的主版本号 */
    ver[1] = 23;  /* 请求的次版本号 */

    fuse_send_request(&req, &resp);

    return 0;
}

/* ---- FUSE 注册/注销 ---- */

int fuse_register_fs(const char *mount_point, const char *daemon_path) {
    spinlock_lock(&fuse_lock);

    /* 检查是否已注册 */
    fuse_mount_t *m = fuse_mount_list;
    while (m) {
        if (strcmp(m->mount_point, mount_point) == 0) {
            spinlock_unlock(&fuse_lock);
            return -1;  /* 已存在 */
        }
        m = m->next;
    }

    /* 创建新的挂载实例 */
    fuse_mount_t *fm = (fuse_mount_t *)kmalloc(sizeof(fuse_mount_t));
    if (!fm) {
        spinlock_unlock(&fuse_lock);
        return -1;
    }
    memset(fm, 0, sizeof(fuse_mount_t));
    strncpy(fm->mount_point, mount_point, 255);
    strncpy(fm->daemon_path, daemon_path, 255);
    fm->next_unique = 1;
    fm->active = 1;

    /* 添加到链表 */
    fm->next = fuse_mount_list;
    fuse_mount_list = fm;

    spinlock_unlock(&fuse_lock);

    /* 通过 VFS 挂载 */
    return vfs_mount(mount_point, FS_TYPE_FUSE, (void *)fm);
}

int fuse_unregister_fs(const char *mount_point) {
    spinlock_lock(&fuse_lock);

    fuse_mount_t *prev = NULL;
    fuse_mount_t *m = fuse_mount_list;

    while (m) {
        if (strcmp(m->mount_point, mount_point) == 0) {
            if (prev) {
                prev->next = m->next;
            } else {
                fuse_mount_list = m->next;
            }
            m->active = 0;
            kfree(m);
            spinlock_unlock(&fuse_lock);
            return vfs_umount(mount_point);
        }
        prev = m;
        m = m->next;
    }

    spinlock_unlock(&fuse_lock);
    return -1;
}

/* ---- 初始化 ---- */

void fuse_init(void) {
    spinlock_init(&fuse_lock);
    fuse_mount_list = NULL;
    fuse_global_unique = 1;
}
