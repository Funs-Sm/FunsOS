/*
 * fuse.c - FUSE (Filesystem in Userspace) 框架实现
 *
 * 提供内核侧 FUSE 支持，将 VFS 操作转换为 FUSE 请求，
 * 通过 /dev/fuse 设备与用户态守护进程通信。
 */

#include "fuse.h"
#include "devfs.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "spinlock.h"
#include "klog.h"
#include "stddef.h"

static fuse_mount_t *fuse_mount_list = NULL;
static spinlock_t fuse_lock;
static uint32_t fuse_global_unique = 1;
static fuse_mount_t *fuse_dev_mount = NULL;

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

    uint32_t *args = (uint32_t *)req.data;
    args[0] = file->offset;
    args[1] = count;
    args[2] = (uint32_t)(uintptr_t)file->private_data;

    if (fuse_send_request(&req, &resp) != 0 || resp.error != 0) {
        return resp.error ? resp.error : -1;
    }

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
    return -ENOSYS;
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
            return -EINVAL;
    }

    if (new_offset < 0) return -EINVAL;
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

    uint32_t *attrs = (uint32_t *)resp.data;
    ino->ino = attrs[0];
    ino->mode = attrs[1];
    ino->size = attrs[2];
    ino->nlinks = 1;

    d->inode = ino;
    return d;
}

static int32_t fuse_inode_getattr(inode_t *inode) {
    if (!inode) return -1;

    fuse_request_t req;
    fuse_response_t resp;

    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_GETATTR;
    req.nodeid = inode->ino;
    req.data_len = 0;

    if (fuse_send_request(&req, &resp) != 0 || resp.error != 0) {
        return -1;
    }

    uint32_t *attrs = (uint32_t *)resp.data;
    inode->mode = attrs[1];
    inode->size = attrs[2];
    inode->nlinks = attrs[3];
    inode->uid = attrs[4];
    inode->gid = attrs[5];

    return 0;
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
    .link     = NULL,
};

/* ---- 请求/响应队列操作 ---- */

int fuse_enqueue_request(fuse_mount_t *fm, fuse_request_t *req) {
    if (!fm || !req) return -1;
    if (fm->req_queue_count >= FUSE_MAX_REQ_QUEUE) return -1;

    fuse_req_node_t *node = (fuse_req_node_t *)kmalloc(sizeof(fuse_req_node_t));
    if (!node) return -1;
    memset(node, 0, sizeof(fuse_req_node_t));
    memcpy(&node->req, req, sizeof(fuse_request_t));
    node->next = NULL;

    if (fm->req_queue_tail) {
        fm->req_queue_tail->next = node;
    } else {
        fm->req_queue_head = node;
    }
    fm->req_queue_tail = node;
    fm->req_queue_count++;

    return 0;
}

int fuse_dequeue_request(fuse_mount_t *fm, fuse_request_t *req) {
    if (!fm || !req) return -1;
    if (!fm->req_queue_head) return -1;

    fuse_req_node_t *node = fm->req_queue_head;
    fm->req_queue_head = node->next;
    if (!fm->req_queue_head) {
        fm->req_queue_tail = NULL;
    }
    fm->req_queue_count--;

    memcpy(req, &node->req, sizeof(fuse_request_t));
    kfree(node);

    return 0;
}

int fuse_enqueue_response(fuse_mount_t *fm, fuse_response_t *resp) {
    if (!fm || !resp) return -1;
    if (fm->resp_queue_count >= FUSE_MAX_RESP_QUEUE) return -1;

    fuse_resp_node_t *node = (fuse_resp_node_t *)kmalloc(sizeof(fuse_resp_node_t));
    if (!node) return -1;
    memset(node, 0, sizeof(fuse_resp_node_t));
    memcpy(&node->resp, resp, sizeof(fuse_response_t));
    node->next = NULL;

    if (fm->resp_queue_tail) {
        fm->resp_queue_tail->next = node;
    } else {
        fm->resp_queue_head = node;
    }
    fm->resp_queue_tail = node;
    fm->resp_queue_count++;

    return 0;
}

int fuse_dequeue_response(fuse_mount_t *fm, fuse_response_t *resp) {
    if (!fm || !resp) return -1;
    if (!fm->resp_queue_head) return -1;

    fuse_resp_node_t *node = fm->resp_queue_head;
    fm->resp_queue_head = node->next;
    if (!fm->resp_queue_head) {
        fm->resp_queue_tail = NULL;
    }
    fm->resp_queue_count--;

    memcpy(resp, &node->resp, sizeof(fuse_response_t));
    kfree(node);

    return 0;
}

/* ---- FUSE 守护进程通信 ---- */

int fuse_send_request(fuse_request_t *req, fuse_response_t *resp) {
    if (!req || !resp) return -1;

    memset(resp, 0, sizeof(fuse_response_t));
    resp->unique = req->unique;

    if (req->opcode == FUSE_INIT) {
        resp->error = 0;
        uint32_t *ver = (uint32_t *)resp->data;
        ver[0] = FUSE_VERSION_MAJOR;
        ver[1] = FUSE_VERSION_MINOR;
        ver[2] = 4096;
        ver[3] = 0;
        resp->data_len = sizeof(uint32_t) * 4;
        klog_info("fuse: FUSE_INIT handshake complete (v%d.%d)",
                  FUSE_VERSION_MAJOR, FUSE_VERSION_MINOR);
        return 0;
    }

    resp->error = -ENOSYS;
    resp->data_len = 0;

    return 0;
}

/* ---- /dev/fuse 设备操作 ---- */

static int32_t fuse_dev_open(inode_t *inode, file_t *file) {
    (void)inode;
    (void)file;
    klog_info("fuse: /dev/fuse opened");
    return 0;
}

int fuse_dev_read(file_t *file, void *buf, uint32_t count) {
    (void)file;
    if (!buf || !fuse_dev_mount) return -1;

    fuse_request_t req;
    if (fuse_dequeue_request(fuse_dev_mount, &req) != 0) {
        return 0;
    }

    uint32_t to_copy = sizeof(fuse_request_t);
    if (to_copy > count) to_copy = count;
    memcpy(buf, &req, to_copy);

    klog_debug("fuse: /dev/fuse read request opcode=%u unique=%u",
               req.opcode, req.unique);
    return (int32_t)to_copy;
}

int fuse_dev_write(file_t *file, const void *buf, uint32_t count) {
    (void)file;
    if (!buf || !fuse_dev_mount) return -1;
    if (count < sizeof(fuse_response_t)) return -EINVAL;

    fuse_response_t resp;
    memcpy(&resp, buf, sizeof(fuse_response_t));

    if (fuse_enqueue_response(fuse_dev_mount, &resp) != 0) {
        return -ENOMEM;
    }

    klog_debug("fuse: /dev/fuse write response unique=%u error=%d",
               resp.unique, resp.error);
    return (int32_t)count;
}

static int32_t fuse_dev_close(file_t *file) {
    (void)file;
    klog_info("fuse: /dev/fuse closed");
    return 0;
}

int fuse_dev_ioctl(file_t *file, uint32_t cmd, void *arg) {
    (void)file;
    (void)cmd;
    (void)arg;
    return -ENOSYS;
}

static file_ops_t fuse_dev_ops = {
    .open   = fuse_dev_open,
    .read   = fuse_dev_read,
    .write  = fuse_dev_write,
    .close  = fuse_dev_close,
    .seek   = NULL,
    .ioctl  = fuse_dev_ioctl,
};

/* ---- FUSE 挂载 ---- */

int fuse_mount(superblock_t *sb, void *data) {
    if (!sb) return -1;

    sb->fs_type = FS_TYPE_FUSE;
    sb->block_size = 4096;
    sb->fs_data = data;

    inode_t *root = (inode_t *)kmalloc(sizeof(inode_t));
    if (!root) return -1;
    memset(root, 0, sizeof(inode_t));
    root->ino = 1;
    root->mode = FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_WRITE;
    root->ops = &fuse_inode_ops;
    root->sb = sb;

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

    fuse_request_t req;
    fuse_response_t resp;
    memset(&req, 0, sizeof(req));
    req.unique = fuse_global_unique++;
    req.opcode = FUSE_INIT;
    req.data_len = sizeof(uint32_t) * 2;
    uint32_t *ver = (uint32_t *)req.data;
    ver[0] = FUSE_VERSION_MAJOR;
    ver[1] = FUSE_VERSION_MINOR;

    fuse_send_request(&req, &resp);

    if (data) {
        fuse_mount_t *fm = (fuse_mount_t *)data;
        fm->sb = sb;
        fm->initialized = 1;
        fm->proto_major = FUSE_VERSION_MAJOR;
        fm->proto_minor = FUSE_VERSION_MINOR;
    }

    klog_info("fuse: filesystem mounted");
    return 0;
}

/* ---- FUSE 注册/注销 ---- */

fuse_mount_t *fuse_find_mount(const char *mount_point) {
    fuse_mount_t *m = fuse_mount_list;
    while (m) {
        if (strcmp(m->mount_point, mount_point) == 0) return m;
        m = m->next;
    }
    return NULL;
}

int fuse_register_fs(const char *mount_point, const char *daemon_path) {
    spinlock_lock(&fuse_lock);

    if (fuse_find_mount(mount_point)) {
        spinlock_unlock(&fuse_lock);
        return -EEXIST;
    }

    fuse_mount_t *fm = (fuse_mount_t *)kmalloc(sizeof(fuse_mount_t));
    if (!fm) {
        spinlock_unlock(&fuse_lock);
        return -ENOMEM;
    }
    memset(fm, 0, sizeof(fuse_mount_t));
    strncpy(fm->mount_point, mount_point, 255);
    strncpy(fm->daemon_path, daemon_path, 255);
    fm->next_unique = 1;
    fm->active = 1;
    fm->initialized = 0;

    fm->next = fuse_mount_list;
    fuse_mount_list = fm;

    spinlock_unlock(&fuse_lock);

    int ret = vfs_mount(mount_point, FS_TYPE_FUSE, (void *)fm);
    if (ret != 0) {
        klog_err("fuse: failed to mount at %s, ret=%d", mount_point, ret);
        return ret;
    }

    klog_info("fuse: registered filesystem at %s (daemon: %s)",
              mount_point, daemon_path);
    return 0;
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

            while (m->req_queue_head) {
                fuse_req_node_t *next = m->req_queue_head->next;
                kfree(m->req_queue_head);
                m->req_queue_head = next;
            }
            while (m->resp_queue_head) {
                fuse_resp_node_t *next = m->resp_queue_head->next;
                kfree(m->resp_queue_head);
                m->resp_queue_head = next;
            }

            kfree(m);
            spinlock_unlock(&fuse_lock);

            klog_info("fuse: unregistered filesystem at %s", mount_point);
            return vfs_umount(mount_point);
        }
        prev = m;
        m = m->next;
    }

    spinlock_unlock(&fuse_lock);
    return -ENOENT;
}

/* ---- 初始化 ---- */

void fuse_init(void) {
    spinlock_init(&fuse_lock);
    fuse_mount_list = NULL;
    fuse_global_unique = 1;

    fuse_dev_mount = (fuse_mount_t *)kmalloc(sizeof(fuse_mount_t));
    if (fuse_dev_mount) {
        memset(fuse_dev_mount, 0, sizeof(fuse_mount_t));
        strncpy(fuse_dev_mount->mount_point, "/dev/fuse", 255);
        fuse_dev_mount->active = 1;
        fuse_dev_mount->initialized = 1;
    }

    int ret = devfs_register_with_perm("fuse", DEVICE_CHAR,
                                        FUSE_DEV_MAJOR, FUSE_DEV_MINOR,
                                        &fuse_dev_ops, NULL,
                                        0666, 0, 0);
    if (ret == 0) {
        klog_info("fuse: /dev/fuse device registered (%d,%d)",
                  FUSE_DEV_MAJOR, FUSE_DEV_MINOR);
    } else {
        klog_err("fuse: failed to register /dev/fuse device");
    }

    klog_info("fuse: initialization complete");
}
