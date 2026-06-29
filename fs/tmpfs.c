#include "tmpfs.h"
#include "vfs.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"
#include "sync.h"
#include "spinlock.h"
#include "../kernel/klog.h"

static superblock_t *tmpfs_sb;
static tmpfs_sb_info_t *tmpfs_sb_info;
static spinlock_t tmpfs_lock;
static uint32_t tmpfs_next_ino = 1;

static dentry_t *tmpfs_lookup_op(dentry_t *dir, const char *name);
static int32_t tmpfs_create_op(dentry_t *dir, const char *name, uint32_t mode);
static int32_t tmpfs_mkdir_op(dentry_t *dir, const char *name, uint32_t mode);
static int32_t tmpfs_unlink_op(dentry_t *dir, const char *name);
static int32_t tmpfs_rmdir_op(dentry_t *dir, const char *name);
static int32_t tmpfs_rename_op(dentry_t *old_dir, const char *old_name,
                                dentry_t *new_dir, const char *new_name);
static int32_t tmpfs_symlink_op(dentry_t *dir, const char *name, const char *target);
static int32_t tmpfs_readlink_op(dentry_t *dentry, char *buf, uint32_t size);

static int32_t tmpfs_file_open(inode_t *inode, file_t *file);
static int32_t tmpfs_file_read(file_t *file, void *buf, uint32_t count);
static int32_t tmpfs_file_write(file_t *file, const void *buf, uint32_t count);
static int32_t tmpfs_file_close(file_t *file);
static int32_t tmpfs_file_seek(file_t *file, int32_t offset, int32_t whence);
static int32_t tmpfs_file_ioctl(file_t *file, uint32_t cmd, void *arg);

static inode_ops_t tmpfs_inode_ops = {
    .lookup  = tmpfs_lookup_op,
    .create  = tmpfs_create_op,
    .mkdir   = tmpfs_mkdir_op,
    .unlink  = tmpfs_unlink_op,
    .rmdir   = tmpfs_rmdir_op,
    .rename  = tmpfs_rename_op,
    .readlink = tmpfs_readlink_op,
    .symlink  = tmpfs_symlink_op,
};

file_ops_t tmpfs_file_ops = {
    .open  = tmpfs_file_open,
    .read  = tmpfs_file_read,
    .write = tmpfs_file_write,
    .close = tmpfs_file_close,
    .seek  = tmpfs_file_seek,
    .ioctl = tmpfs_file_ioctl,
};

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static uint32_t tmpfs_alloc_ino(void) {
    return tmpfs_next_ino++;
}

static tmpfs_node_t *tmpfs_find_child(tmpfs_node_t *dir, const char *name) {
    if (!dir || !(dir->mode & FILE_MODE_DIR)) return NULL;
    tmpfs_node_t *c = dir->child;
    while (c) {
        if (strcmp(c->name, name) == 0) return c;
        c = c->next_sibling;
    }
    return NULL;
}

static void tmpfs_unlink_from_parent(tmpfs_node_t *node) {
    if (!node || !node->parent) return;
    tmpfs_node_t *prev = NULL;
    tmpfs_node_t *cur  = node->parent->child;
    while (cur) {
        if (cur == node) {
            if (prev) prev->next_sibling = cur->next_sibling;
            else      node->parent->child = cur->next_sibling;
            return;
        }
        prev = cur;
        cur  = cur->next_sibling;
    }
}

static int32_t tmpfs_check_space(uint32_t needed) {
    if (!tmpfs_sb_info) return -1;
    if (tmpfs_sb_info->used_size + needed > tmpfs_sb_info->max_size) {
        return -ENOMEM;
    }
    return 0;
}

static void tmpfs_add_used(uint32_t size) {
    if (tmpfs_sb_info) {
        tmpfs_sb_info->used_size += size;
    }
}

static void tmpfs_sub_used(uint32_t size) {
    if (tmpfs_sb_info && tmpfs_sb_info->used_size >= size) {
        tmpfs_sb_info->used_size -= size;
    }
}

static int32_t tmpfs_grow_data(tmpfs_node_t *node, uint32_t new_size) {
    if (new_size <= node->capacity) return 0;

    uint32_t cap = node->capacity ? node->capacity : TMPFS_INITIAL_CAP;
    while (cap < new_size) cap *= 2;

    uint32_t additional = node->capacity > 0 ? (cap - node->capacity) : cap;
    if (tmpfs_check_space(additional) != 0) {
        return -ENOMEM;
    }

    uint8_t *buf = (uint8_t *)kmalloc(cap);
    if (!buf) return -1;

    if (node->data && node->size > 0) {
        memcpy(buf, node->data, node->size);
    }

    if (node->data) {
        tmpfs_sub_used(node->capacity);
        kfree(node->data);
    }

    tmpfs_add_used(cap);

    node->data = buf;
    node->capacity = cap;
    return 0;
}

static dentry_t *tmpfs_build_dentry(dentry_t *parent, tmpfs_node_t *node, const char *name) {
    inode_t *inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!inode) return NULL;
    memset(inode, 0, sizeof(inode_t));

    dentry_t *d = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!d) {
        kfree(inode);
        return NULL;
    }
    memset(d, 0, sizeof(dentry_t));

    inode->ino = node->ino;
    inode->mode = node->mode;
    inode->uid = node->uid;
    inode->gid = node->gid;
    inode->size = node->size;
    inode->nlinks = node->nlinks;
    inode->atime = node->atime;
    inode->mtime = node->mtime;
    inode->ctime = node->ctime;
    inode->sb = tmpfs_sb;
    inode->ops = &tmpfs_inode_ops;
    inode->private_data = node;
    inode->dentries = d;

    strncpy(d->name, name, 255);
    d->name[255] = '\0';
    d->parent = parent;
    d->inode = inode;

    d->next_sibling = parent->child;
    parent->child = d;

    return d;
}

static void tmpfs_remove_vfs_dentry(dentry_t *parent, const char *name) {
    if (!parent || !name) return;

    dentry_t *prev = NULL;
    dentry_t *cur = parent->child;
    while (cur) {
        if (strcmp(cur->name, name) == 0) {
            if (prev) {
                prev->next_sibling = cur->next_sibling;
            } else {
                parent->child = cur->next_sibling;
            }
            if (cur->inode) {
                cur->inode->dentries = NULL;
                kfree(cur->inode);
            }
            kfree(cur);
            return;
        }
        prev = cur;
        cur = cur->next_sibling;
    }
}

static tmpfs_node_t *tmpfs_new_node(const char *name, uint32_t mode) {
    if (tmpfs_check_space(sizeof(tmpfs_node_t)) != 0) {
        return NULL;
    }

    tmpfs_node_t *n = (tmpfs_node_t *)kmalloc(sizeof(tmpfs_node_t));
    if (!n) return NULL;
    memset(n, 0, sizeof(tmpfs_node_t));
    n->ino  = tmpfs_alloc_ino();
    n->mode = mode;
    n->uid  = 0;
    n->gid  = 0;
    n->nlinks = (mode & FILE_MODE_DIR) ? 2 : 1;
    strncpy(n->name, name, TMPFS_MAX_NAME);
    n->name[TMPFS_MAX_NAME] = '\0';

    tmpfs_add_used(sizeof(tmpfs_node_t));
    if (tmpfs_sb_info) {
        tmpfs_sb_info->inode_count++;
    }

    return n;
}

static void tmpfs_free_node(tmpfs_node_t *node) {
    if (!node) return;

    if (node->data) {
        tmpfs_sub_used(node->capacity);
        kfree(node->data);
    }

    tmpfs_sub_used(sizeof(tmpfs_node_t));
    if (tmpfs_sb_info && tmpfs_sb_info->inode_count > 0) {
        tmpfs_sb_info->inode_count--;
    }

    kfree(node);
}

/* ------------------------------------------------------------------ */
/* inode_ops                                                          */
/* ------------------------------------------------------------------ */

static dentry_t *tmpfs_lookup_op(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode || !dir->inode->private_data || !name) return NULL;
    tmpfs_node_t *d = (tmpfs_node_t *)dir->inode->private_data;
    if (!(d->mode & FILE_MODE_DIR)) return NULL;
    if (strcmp(name, ".") == 0) return dir;
    if (strcmp(name, "..") == 0) return dir->parent;

    dentry_t *child = dir->child;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
        child = child->next_sibling;
    }
    return NULL;
}

static int32_t tmpfs_create_op(dentry_t *dir, const char *name, uint32_t mode) {
    if (!dir || !dir->inode || !dir->inode->private_data || !name) return -1;
    tmpfs_node_t *d = (tmpfs_node_t *)dir->inode->private_data;
    if (!(d->mode & FILE_MODE_DIR)) return -1;
    if (tmpfs_find_child(d, name)) return -EEXIST;

    tmpfs_node_t *n = tmpfs_new_node(name, mode | FILE_MODE_REG);
    if (!n) return -ENOMEM;

    n->parent = d;
    n->next_sibling = d->child;
    d->child = n;

    if (!tmpfs_build_dentry(dir, n, name)) {
        tmpfs_unlink_from_parent(n);
        tmpfs_free_node(n);
        return -1;
    }
    return 0;
}

static int32_t tmpfs_mkdir_op(dentry_t *dir, const char *name, uint32_t mode) {
    if (!dir || !dir->inode || !dir->inode->private_data || !name) return -1;
    tmpfs_node_t *d = (tmpfs_node_t *)dir->inode->private_data;
    if (!(d->mode & FILE_MODE_DIR)) return -ENOTDIR;
    if (tmpfs_find_child(d, name)) return -EEXIST;

    tmpfs_node_t *n = tmpfs_new_node(name, (mode & ~FILE_MODE_REG) | FILE_MODE_DIR);
    if (!n) return -ENOMEM;

    n->parent = d;
    n->next_sibling = d->child;
    d->child = n;
    d->nlinks++;

    if (!tmpfs_build_dentry(dir, n, name)) {
        d->nlinks--;
        tmpfs_unlink_from_parent(n);
        tmpfs_free_node(n);
        return -1;
    }
    return 0;
}

static int32_t tmpfs_remove_node(tmpfs_node_t *parent, tmpfs_node_t *node) {
    if (node->mode & FILE_MODE_DIR) {
        if (node->child) return -ENOTEMPTY;
        if (parent->nlinks > 0) parent->nlinks--;
    }
    tmpfs_unlink_from_parent(node);
    tmpfs_free_node(node);
    return 0;
}

static int32_t tmpfs_unlink_op(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode || !dir->inode->private_data || !name) return -1;
    tmpfs_node_t *d = (tmpfs_node_t *)dir->inode->private_data;
    tmpfs_node_t *c = tmpfs_find_child(d, name);
    if (!c) return -ENOENT;
    if (c->mode & FILE_MODE_DIR) return -EISDIR;
    int32_t ret = tmpfs_remove_node(d, c);
    if (ret == 0) {
        tmpfs_remove_vfs_dentry(dir, name);
    }
    return ret;
}

static int32_t tmpfs_rmdir_op(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode || !dir->inode->private_data || !name) return -1;
    tmpfs_node_t *d = (tmpfs_node_t *)dir->inode->private_data;
    tmpfs_node_t *c = tmpfs_find_child(d, name);
    if (!c) return -ENOENT;
    if (!(c->mode & FILE_MODE_DIR)) return -ENOTDIR;
    int32_t ret = tmpfs_remove_node(d, c);
    if (ret == 0) {
        tmpfs_remove_vfs_dentry(dir, name);
    }
    return ret;
}

static int32_t tmpfs_rename_op(dentry_t *old_dir, const char *old_name,
                                dentry_t *new_dir, const char *new_name) {
    if (!old_dir || !old_dir->inode || !old_dir->inode->private_data || !old_name) return -1;
    if (!new_dir || !new_dir->inode || !new_dir->inode->private_data || !new_name) return -1;
    tmpfs_node_t *od = (tmpfs_node_t *)old_dir->inode->private_data;
    tmpfs_node_t *nd = (tmpfs_node_t *)new_dir->inode->private_data;
    tmpfs_node_t *node = tmpfs_find_child(od, old_name);
    if (!node) return -ENOENT;
    if (tmpfs_find_child(nd, new_name)) return -EEXIST;

    tmpfs_unlink_from_parent(node);
    strncpy(node->name, new_name, TMPFS_MAX_NAME);
    node->name[TMPFS_MAX_NAME] = '\0';
    node->parent = nd;
    node->next_sibling = nd->child;
    nd->child = node;

    tmpfs_remove_vfs_dentry(old_dir, old_name);
    if (!tmpfs_build_dentry(new_dir, node, new_name)) {
        tmpfs_unlink_from_parent(node);
        strncpy(node->name, old_name, TMPFS_MAX_NAME);
        node->name[TMPFS_MAX_NAME] = '\0';
        node->parent = od;
        node->next_sibling = od->child;
        od->child = node;
        return -1;
    }
    return 0;
}

static int32_t tmpfs_symlink_op(dentry_t *dir, const char *name, const char *target) {
    if (!dir || !dir->inode || !name || !target) return -1;

    tmpfs_node_t *parent = (tmpfs_node_t *)dir->inode->private_data;
    if (!parent) return -1;
    if (tmpfs_find_child(parent, name)) return -EEXIST;

    uint32_t target_len = strlen(target);
    uint32_t needed = target_len + 1;

    tmpfs_node_t *node = tmpfs_new_node(name, FILE_MODE_LNK | FILE_MODE_READ);
    if (!node) return -ENOMEM;

    if (tmpfs_check_space(needed) != 0) {
        tmpfs_free_node(node);
        return -ENOMEM;
    }

    node->size = target_len;
    node->capacity = needed;
    node->data = (uint8_t *)kmalloc(needed);
    if (!node->data) {
        tmpfs_free_node(node);
        return -ENOMEM;
    }
    memcpy(node->data, target, target_len + 1);
    tmpfs_add_used(needed);

    node->parent = parent;
    node->next_sibling = parent->child;
    parent->child = node;

    if (!tmpfs_build_dentry(dir, node, name)) {
        tmpfs_unlink_from_parent(node);
        tmpfs_sub_used(needed);
        kfree(node->data);
        tmpfs_free_node(node);
        return -1;
    }
    return 0;
}

static int32_t tmpfs_readlink_op(dentry_t *dentry, char *buf, uint32_t size) {
    if (!dentry || !dentry->inode || !buf || size == 0) return -1;

    tmpfs_node_t *node = (tmpfs_node_t *)dentry->inode->private_data;
    if (!node) return -1;

    if (!(node->mode & FILE_MODE_LNK)) return -EINVAL;

    uint32_t link_len = node->size;
    if (link_len == 0) return -1;
    if (link_len > size - 1) link_len = size - 1;

    memcpy(buf, node->data, link_len);
    buf[link_len] = '\0';

    return (int32_t)link_len;
}

/* ------------------------------------------------------------------ */
/* file_ops                                                           */
/* ------------------------------------------------------------------ */

static int32_t tmpfs_file_open(inode_t *inode, file_t *file) {
    (void)inode;
    file->offset = 0;
    return 0;
}

static int32_t tmpfs_file_read(file_t *file, void *buf, uint32_t count) {
    if (!file || !file->inode || !file->inode->private_data) return -EBADF;
    tmpfs_node_t *n = (tmpfs_node_t *)file->inode->private_data;
    if (!n->data) return 0;
    if (file->offset >= n->size) return 0;
    uint32_t avail = n->size - file->offset;
    uint32_t to_copy = (count < avail) ? count : avail;
    memcpy(buf, n->data + file->offset, to_copy);
    file->offset += to_copy;
    return (int32_t)to_copy;
}

static int32_t tmpfs_file_write(file_t *file, const void *buf, uint32_t count) {
    if (!file || !file->inode || !file->inode->private_data) return -EBADF;
    tmpfs_node_t *n = (tmpfs_node_t *)file->inode->private_data;
    if (n->mode & FILE_MODE_DIR) return -EISDIR;
    uint32_t end = file->offset + count;
    if (end < file->offset) return -EINVAL;
    if (tmpfs_grow_data(n, end) != 0) return -ENOMEM;
    memcpy(n->data + file->offset, buf, count);
    file->offset += count;
    if (file->offset > n->size) n->size = file->offset;
    if (file->inode) {
        file->inode->size = n->size;
    }
    return (int32_t)count;
}

static int32_t tmpfs_file_close(file_t *file) {
    (void)file;
    return 0;
}

static int32_t tmpfs_file_seek(file_t *file, int32_t offset, int32_t whence) {
    if (!file || !file->inode || !file->inode->private_data) return -EBADF;
    tmpfs_node_t *n = (tmpfs_node_t *)file->inode->private_data;
    int32_t new_off;
    switch (whence) {
        case SEEK_SET: new_off = offset; break;
        case SEEK_CUR: new_off = (int32_t)file->offset + offset; break;
        case SEEK_END: new_off = (int32_t)n->size + offset; break;
        default: return -EINVAL;
    }
    if (new_off < 0) return -EINVAL;
    file->offset = (uint32_t)new_off;
    return new_off;
}

static int32_t tmpfs_file_ioctl(file_t *file, uint32_t cmd, void *arg) {
    if (!file || !file->inode) return -EBADF;
    switch (cmd) {
        case FIONREAD:
            if (arg) {
                int32_t avail = (int32_t)file->inode->size - (int32_t)file->offset;
                if (avail < 0) avail = 0;
                *(int32_t *)arg = avail;
                return 0;
            }
            return -EINVAL;
        case FIONBIO:
            return 0;
        default:
            return -ENOSYS;
    }
}

int32_t tmpfs_file_truncate(inode_t *inode, uint32_t size) {
    if (!inode || !inode->private_data) return -1;
    tmpfs_node_t *n = (tmpfs_node_t *)inode->private_data;
    if (size > n->capacity) {
        if (tmpfs_grow_data(n, size) != 0) return -ENOMEM;
    }
    if (size > n->size && n->data) {
        memset(n->data + n->size, 0, size - n->size);
    }
    n->size = size;
    return 0;
}

/* ------------------------------------------------------------------ */
/* mount / init                                                       */
/* ------------------------------------------------------------------ */

int32_t tmpfs_mount(superblock_t *sb, void *data) {
    spinlock_init(&tmpfs_lock);

    tmpfs_sb_info = (tmpfs_sb_info_t *)kmalloc(sizeof(tmpfs_sb_info_t));
    if (!tmpfs_sb_info) return -ENOMEM;
    memset(tmpfs_sb_info, 0, sizeof(tmpfs_sb_info_t));

    if (data) {
        tmpfs_sb_info->max_size = *(uint32_t *)data;
    } else {
        tmpfs_sb_info->max_size = TMPFS_DEFAULT_SIZE;
    }
    tmpfs_sb_info->used_size = 0;
    tmpfs_sb_info->inode_count = 0;

    tmpfs_node_t *root = tmpfs_new_node("/", FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_WRITE);
    if (!root) {
        kfree(tmpfs_sb_info);
        tmpfs_sb_info = NULL;
        return -ENOMEM;
    }
    root->parent = root;
    tmpfs_sb_info->root = root;

    inode_t *root_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!root_inode) {
        tmpfs_free_node(root);
        kfree(tmpfs_sb_info);
        tmpfs_sb_info = NULL;
        return -ENOMEM;
    }
    memset(root_inode, 0, sizeof(inode_t));
    root_inode->ino  = root->ino;
    root_inode->mode = root->mode;
    root_inode->size = 0;
    root_inode->nlinks = 2;
    root_inode->sb    = sb;
    root_inode->ops   = &tmpfs_inode_ops;
    root_inode->private_data = root;

    sb->fs_type     = FS_TYPE_RAMFS;
    sb->fs_data     = tmpfs_sb_info;
    sb->block_size  = 4096;
    sb->total_blocks = tmpfs_sb_info->max_size / 4096;
    sb->free_blocks  = (tmpfs_sb_info->max_size - tmpfs_sb_info->used_size) / 4096;
    sb->root = root_inode;

    tmpfs_sb = sb;
    return 0;
}

int32_t tmpfs_init(void) {
    return vfs_mount("/tmp", FS_TYPE_RAMFS, NULL);
}

/* ------------------------------------------------------------------ */
/* statistics                                                         */
/* ------------------------------------------------------------------ */

static void tmpfs_collect(tmpfs_node_t *n, tmpfs_stats_t *s) {
    if (!n) return;
    s->node_count++;
    if (n->mode & FILE_MODE_DIR) s->dir_count++;
    else                         s->file_count++;
    tmpfs_node_t *c = n->child;
    while (c) {
        tmpfs_collect(c, s);
        c = c->next_sibling;
    }
}

int32_t tmpfs_get_stats(tmpfs_stats_t *out) {
    if (!out || !tmpfs_sb_info || !tmpfs_sb_info->root) return -1;
    memset(out, 0, sizeof(tmpfs_stats_t));
    out->total_bytes = tmpfs_sb_info->max_size;
    out->used_bytes = tmpfs_sb_info->used_size;
    out->free_bytes = tmpfs_sb_info->max_size - tmpfs_sb_info->used_size;
    tmpfs_collect(tmpfs_sb_info->root, out);
    return 0;
}
