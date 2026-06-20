#include "ramfs.h"
#include "vfs.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"
#include "sync.h"
#include "spinlock.h"
#include "../kernel/klog.h"

static superblock_t *ramfs_sb;
static ramfs_node_t *ramfs_root;
static spinlock_t ramfs_lock;
static uint32_t ramfs_next_ino = 1;

static dentry_t *ramfs_lookup_op(dentry_t *dir, const char *name);
static int32_t ramfs_create_op(dentry_t *dir, const char *name, uint32_t mode);
static int32_t ramfs_mkdir_op(dentry_t *dir, const char *name, uint32_t mode);
static int32_t ramfs_unlink_op(dentry_t *dir, const char *name);
static int32_t ramfs_rmdir_op(dentry_t *dir, const char *name);
static int32_t ramfs_rename_op(dentry_t *old_dir, const char *old_name,
                                dentry_t *new_dir, const char *new_name);
static int32_t ramfs_symlink_op(dentry_t *dir, const char *name, const char *target);
static int32_t ramfs_readlink_op(dentry_t *dentry, char *buf, uint32_t size);

static int32_t ramfs_file_open(inode_t *inode, file_t *file);
static int32_t ramfs_file_read(file_t *file, void *buf, uint32_t count);
static int32_t ramfs_file_write(file_t *file, const void *buf, uint32_t count);
static int32_t ramfs_file_close(file_t *file);
static int32_t ramfs_file_seek(file_t *file, int32_t offset, int32_t whence);

static inode_ops_t ramfs_inode_ops = {
    .lookup  = ramfs_lookup_op,
    .create  = ramfs_create_op,
    .mkdir   = ramfs_mkdir_op,
    .unlink  = ramfs_unlink_op,
    .rmdir   = ramfs_rmdir_op,
    .rename  = ramfs_rename_op,
    .readlink = ramfs_readlink_op,
    .symlink  = ramfs_symlink_op,
};

file_ops_t ramfs_file_ops = {
    .open  = ramfs_file_open,
    .read  = ramfs_file_read,
    .write = ramfs_file_write,
    .close = ramfs_file_close,
    .seek  = ramfs_file_seek,
    .ioctl = NULL,
};

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static uint32_t ramfs_alloc_ino(void) {
    return ramfs_next_ino++;
}

static ramfs_node_t *ramfs_find_child(ramfs_node_t *dir, const char *name) {
    if (!dir || !(dir->mode & FILE_MODE_DIR)) return NULL;
    ramfs_node_t *c = dir->child;
    while (c) {
        if (strcmp(c->name, name) == 0) return c;
        c = c->next_sibling;
    }
    return NULL;
}

static void ramfs_unlink_from_parent(ramfs_node_t *node) {
    if (!node || !node->parent) return;
    ramfs_node_t *prev = NULL;
    ramfs_node_t *cur  = node->parent->child;
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

static int32_t ramfs_grow_data(ramfs_node_t *node, uint32_t new_size) {
    if (new_size <= node->capacity) return 0;
    uint32_t cap = node->capacity ? node->capacity : RAMFS_INITIAL_CAP;
    while (cap < new_size) cap *= 2;
    uint8_t *buf = (uint8_t *)kmalloc(cap);
    if (!buf) return -1;
    if (node->data && node->size > 0) {
        memcpy(buf, node->data, node->size);
    }
    if (node->data) kfree(node->data);
    node->data = buf;
    node->capacity = cap;
    return 0;
}

/* Build a VFS inode/dentry pair for a ramfs node and attach it to the
 * parent dentry's child list.  This keeps the VFS dentry tree in sync
 * with the ramfs node tree so that path_resolve() can traverse ramfs
 * directories. */
static dentry_t *ramfs_build_dentry(dentry_t *parent, ramfs_node_t *node, const char *name) {
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
    inode->sb = ramfs_sb;
    inode->ops = &ramfs_inode_ops;
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

/* Remove the VFS dentry (and its inode) matching `name` from parent. */
static void ramfs_remove_vfs_dentry(dentry_t *parent, const char *name) {
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

static ramfs_node_t *ramfs_new_node(const char *name, uint32_t mode) {
    ramfs_node_t *n = (ramfs_node_t *)kmalloc(sizeof(ramfs_node_t));
    if (!n) return NULL;
    memset(n, 0, sizeof(ramfs_node_t));
    n->ino  = ramfs_alloc_ino();
    n->mode = mode;
    n->uid  = 0;
    n->gid  = 0;
    n->nlinks = (mode & FILE_MODE_DIR) ? 2 : 1;
    strncpy(n->name, name, RAMFS_MAX_NAME);
    n->name[RAMFS_MAX_NAME] = '\0';
    return n;
}

/* ------------------------------------------------------------------ */
/* inode_ops                                                          */
/* ------------------------------------------------------------------ */

static dentry_t *ramfs_lookup_op(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode || !dir->inode->private_data || !name) return NULL;
    ramfs_node_t *d = (ramfs_node_t *)dir->inode->private_data;
    if (!(d->mode & FILE_MODE_DIR)) return NULL;
    if (strcmp(name, ".") == 0) return dir;
    if (strcmp(name, "..") == 0) return dir->parent;

    /* Walk the VFS dentry child list, which is kept in sync with the
     * ramfs node tree by create/mkdir/symlink. */
    dentry_t *child = dir->child;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
        child = child->next_sibling;
    }
    return NULL;
}

static int32_t ramfs_create_op(dentry_t *dir, const char *name, uint32_t mode) {
    if (!dir || !dir->inode || !dir->inode->private_data || !name) return -1;
    ramfs_node_t *d = (ramfs_node_t *)dir->inode->private_data;
    if (!(d->mode & FILE_MODE_DIR)) return -1;
    if (ramfs_find_child(d, name)) return -1;
    ramfs_node_t *n = ramfs_new_node(name, mode | FILE_MODE_REG);
    if (!n) return -1;
    n->parent = d;
    n->next_sibling = d->child;
    d->child = n;

    if (!ramfs_build_dentry(dir, n, name)) {
        /* Roll back ramfs node creation. */
        ramfs_unlink_from_parent(n);
        kfree(n);
        return -1;
    }
    return 0;
}

static int32_t ramfs_mkdir_op(dentry_t *dir, const char *name, uint32_t mode) {
    if (!dir || !dir->inode || !dir->inode->private_data || !name) return -1;
    ramfs_node_t *d = (ramfs_node_t *)dir->inode->private_data;
    if (!(d->mode & FILE_MODE_DIR)) return -1;
    if (ramfs_find_child(d, name)) return -1;
    ramfs_node_t *n = ramfs_new_node(name, (mode & ~FILE_MODE_REG) | FILE_MODE_DIR);
    if (!n) return -1;
    n->parent = d;
    n->next_sibling = d->child;
    d->child = n;
    d->nlinks++;

    if (!ramfs_build_dentry(dir, n, name)) {
        /* Roll back ramfs node creation. */
        d->nlinks--;
        ramfs_unlink_from_parent(n);
        kfree(n);
        return -1;
    }
    return 0;
}

static int32_t ramfs_remove_node(ramfs_node_t *parent, ramfs_node_t *node) {
    if (node->mode & FILE_MODE_DIR) {
        if (node->child) return -1;
        if (parent->nlinks > 0) parent->nlinks--;
    }
    ramfs_unlink_from_parent(node);
    if (node->data) kfree(node->data);
    kfree(node);
    return 0;
}

static int32_t ramfs_unlink_op(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode || !dir->inode->private_data || !name) return -1;
    ramfs_node_t *d = (ramfs_node_t *)dir->inode->private_data;
    ramfs_node_t *c = ramfs_find_child(d, name);
    if (!c) return -1;
    if (c->mode & FILE_MODE_DIR) return -1;
    int32_t ret = ramfs_remove_node(d, c);
    if (ret == 0) {
        ramfs_remove_vfs_dentry(dir, name);
    }
    return ret;
}

static int32_t ramfs_rmdir_op(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode || !dir->inode->private_data || !name) return -1;
    ramfs_node_t *d = (ramfs_node_t *)dir->inode->private_data;
    ramfs_node_t *c = ramfs_find_child(d, name);
    if (!c) return -1;
    if (!(c->mode & FILE_MODE_DIR)) return -1;
    int32_t ret = ramfs_remove_node(d, c);
    if (ret == 0) {
        ramfs_remove_vfs_dentry(dir, name);
    }
    return ret;
}

static int32_t ramfs_rename_op(dentry_t *old_dir, const char *old_name,
                                dentry_t *new_dir, const char *new_name) {
    if (!old_dir || !old_dir->inode || !old_dir->inode->private_data || !old_name) return -1;
    if (!new_dir || !new_dir->inode || !new_dir->inode->private_data || !new_name) return -1;
    ramfs_node_t *od = (ramfs_node_t *)old_dir->inode->private_data;
    ramfs_node_t *nd = (ramfs_node_t *)new_dir->inode->private_data;
    ramfs_node_t *node = ramfs_find_child(od, old_name);
    if (!node) return -1;
    if (ramfs_find_child(nd, new_name)) return -1;

    ramfs_unlink_from_parent(node);
    strncpy(node->name, new_name, RAMFS_MAX_NAME);
    node->name[RAMFS_MAX_NAME] = '\0';
    node->parent = nd;
    node->next_sibling = nd->child;
    nd->child = node;

    /* Keep the VFS dentry tree in sync: remove the old dentry and
     * create a new one under the destination parent. */
    ramfs_remove_vfs_dentry(old_dir, old_name);
    if (!ramfs_build_dentry(new_dir, node, new_name)) {
        /* Roll back: restore node under the old parent. */
        ramfs_unlink_from_parent(node);
        strncpy(node->name, old_name, RAMFS_MAX_NAME);
        node->parent = od;
        node->next_sibling = od->child;
        od->child = node;
        return -1;
    }
    return 0;
}

static int32_t ramfs_symlink_op(dentry_t *dir, const char *name, const char *target) {
    if (!dir || !dir->inode || !name || !target) return -1;

    ramfs_node_t *parent = (ramfs_node_t *)dir->inode->private_data;
    if (!parent) return -1;
    if (ramfs_find_child(parent, name)) return -1;

    ramfs_node_t *node = ramfs_new_node(name, FILE_MODE_LNK | FILE_MODE_READ);
    if (!node) return -1;

    uint32_t target_len = strlen(target);
    node->size = target_len;
    node->capacity = target_len + 1;
    node->data = (uint8_t *)kmalloc(node->capacity);
    if (!node->data) {
        kfree(node);
        return -1;
    }
    memcpy(node->data, target, target_len + 1);

    node->parent = parent;
    node->next_sibling = parent->child;
    parent->child = node;

    if (!ramfs_build_dentry(dir, node, name)) {
        /* Roll back. */
        ramfs_unlink_from_parent(node);
        kfree(node->data);
        kfree(node);
        return -1;
    }
    return 0;
}

static int32_t ramfs_readlink_op(dentry_t *dentry, char *buf, uint32_t size) {
    if (!dentry || !dentry->inode || !buf || size == 0) return -1;

    ramfs_node_t *node = (ramfs_node_t *)dentry->inode->private_data;
    if (!node) return -1;

    if (!(node->mode & FILE_MODE_LNK)) return -1;

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

static int32_t ramfs_file_open(inode_t *inode, file_t *file) {
    (void)inode;
    file->offset = 0;
    return 0;
}

static int32_t ramfs_file_read(file_t *file, void *buf, uint32_t count) {
    if (!file || !file->inode || !file->inode->private_data) return -1;
    ramfs_node_t *n = (ramfs_node_t *)file->inode->private_data;
    if (!n->data) return 0;
    if (file->offset >= n->size) return 0;
    uint32_t avail = n->size - file->offset;
    uint32_t to_copy = (count < avail) ? count : avail;
    memcpy(buf, n->data + file->offset, to_copy);
    file->offset += to_copy;
    return (int32_t)to_copy;
}

static int32_t ramfs_file_write(file_t *file, const void *buf, uint32_t count) {
    if (!file || !file->inode || !file->inode->private_data) return -1;
    ramfs_node_t *n = (ramfs_node_t *)file->inode->private_data;
    if (n->mode & FILE_MODE_DIR) return -1;
    uint32_t end = file->offset + count;
    if (end < file->offset) return -1;
    if (ramfs_grow_data(n, end) != 0) return -1;
    memcpy(n->data + file->offset, buf, count);
    file->offset += count;
    if (file->offset > n->size) n->size = file->offset;
    return (int32_t)count;
}

static int32_t ramfs_file_close(file_t *file) {
    (void)file;
    return 0;
}

static int32_t ramfs_file_seek(file_t *file, int32_t offset, int32_t whence) {
    if (!file || !file->inode || !file->inode->private_data) return -1;
    ramfs_node_t *n = (ramfs_node_t *)file->inode->private_data;
    int32_t new_off;
    switch (whence) {
        case SEEK_SET: new_off = offset; break;
        case SEEK_CUR: new_off = (int32_t)file->offset + offset; break;
        case SEEK_END: new_off = (int32_t)n->size + offset; break;
        default: return -1;
    }
    if (new_off < 0) return -1;
    file->offset = (uint32_t)new_off;
    return new_off;
}

int32_t ramfs_file_truncate(inode_t *inode, uint32_t size) {
    if (!inode || !inode->private_data) return -1;
    ramfs_node_t *n = (ramfs_node_t *)inode->private_data;
    if (size > n->capacity) {
        if (ramfs_grow_data(n, size) != 0) return -1;
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

int32_t ramfs_mount(superblock_t *sb, void *data) {
    (void)data;
    spinlock_init(&ramfs_lock);

    ramfs_root = ramfs_new_node("/", FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_WRITE);
    if (!ramfs_root) return -1;
    ramfs_root->parent = ramfs_root;

    inode_t *root_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!root_inode) {
        kfree(ramfs_root);
        return -1;
    }
    memset(root_inode, 0, sizeof(inode_t));
    root_inode->ino  = ramfs_root->ino;
    root_inode->mode = ramfs_root->mode;
    root_inode->size = 0;
    root_inode->nlinks = 2;
    root_inode->sb    = sb;
    root_inode->ops   = &ramfs_inode_ops;
    root_inode->private_data = ramfs_root;

    sb->fs_type     = FS_TYPE_RAMFS;
    sb->block_size  = 4096;
    sb->total_blocks = 0;
    sb->free_blocks  = 0;
    sb->root = root_inode;

    ramfs_sb = sb;
    return 0;
}

int32_t ramfs_init(void) {
    return vfs_mount("/", FS_TYPE_RAMFS, NULL);
}

/* ------------------------------------------------------------------ */
/* statistics (used by `df` etc.)                                    */
/* ------------------------------------------------------------------ */

static void ramfs_collect(ramfs_node_t *n, ramfs_stats_t *s) {
    if (!n) return;
    s->node_count++;
    s->total_bytes += n->capacity;
    s->used_bytes  += n->size;
    if (n->mode & FILE_MODE_DIR) s->dir_count++;
    else                         s->file_count++;
    ramfs_node_t *c = n->child;
    while (c) {
        ramfs_collect(c, s);
        c = c->next_sibling;
    }
}

int32_t ramfs_get_stats(ramfs_stats_t *out) {
    if (!out || !ramfs_root) return -1;
    memset(out, 0, sizeof(ramfs_stats_t));
    ramfs_collect(ramfs_root, out);
    return 0;
}
