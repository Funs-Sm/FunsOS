#include "tarfs.h"
#include "vfs.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"
#include "sync.h"
#include "spinlock.h"

/* ------------------------------------------------------------------ */
/* Globals                                                            */
/* ------------------------------------------------------------------ */

static spinlock_t tarfs_lock;
static uint32_t tarfs_next_ino = 1;
static const uint8_t *tarfs_archive_ptr;
static uint32_t tarfs_archive_size;

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
/* ------------------------------------------------------------------ */

static dentry_t *tarfs_lookup_op(dentry_t *dir, const char *name);
static int32_t tarfs_create_op(dentry_t *dir, const char *name, uint32_t mode);
static int32_t tarfs_mkdir_op(dentry_t *dir, const char *name, uint32_t mode);
static int32_t tarfs_unlink_op(dentry_t *dir, const char *name);
static int32_t tarfs_rename_op(dentry_t *old_dir, const char *old_name,
                               dentry_t *new_dir, const char *new_name);

static int32_t tarfs_file_open(inode_t *inode, file_t *file);
static int32_t tarfs_file_read(file_t *file, void *buf, uint32_t count);
static int32_t tarfs_file_close(file_t *file);
static int32_t tarfs_file_seek(file_t *file, int32_t offset, int32_t whence);

/* ------------------------------------------------------------------ */
/* VFS operation tables                                               */
/* ------------------------------------------------------------------ */

static inode_ops_t tarfs_inode_ops = {
    .lookup   = tarfs_lookup_op,
    .create   = tarfs_create_op,
    .mkdir    = tarfs_mkdir_op,
    .unlink   = tarfs_unlink_op,
    .rmdir    = NULL,
    .rename   = tarfs_rename_op,
    .readlink = NULL,
    .symlink  = NULL,
};

file_ops_t tarfs_file_ops = {
    .open  = tarfs_file_open,
    .read  = tarfs_file_read,
    .write = NULL,
    .close = tarfs_file_close,
    .seek  = tarfs_file_seek,
    .ioctl = NULL,
};

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static uint32_t tarfs_alloc_ino(void) {
    return tarfs_next_ino++;
}

static uint32_t octal_to_int(const char *s, uint32_t len) {
    uint32_t result = 0;
    uint32_t i;
    for (i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '7') break;
        result = result * 8 + (uint32_t)(s[i] - '0');
    }
    return result;
}

static tarfs_node_t *tarfs_find_child(tarfs_node_t *dir, const char *name) {
    if (!dir || !(dir->mode & FILE_MODE_DIR)) return NULL;
    tarfs_node_t *c = dir->child;
    while (c) {
        if (strcmp(c->name, name) == 0) return c;
        c = c->next_sibling;
    }
    return NULL;
}

static tarfs_node_t *tarfs_new_node(const char *name, uint32_t mode) {
    tarfs_node_t *n = (tarfs_node_t *)kmalloc(sizeof(tarfs_node_t));
    if (!n) return NULL;
    memset(n, 0, sizeof(tarfs_node_t));
    n->ino  = tarfs_alloc_ino();
    n->mode = mode;
    n->uid  = 0;
    n->gid  = 0;
    n->nlinks = (mode & FILE_MODE_DIR) ? 2 : 1;
    strncpy(n->name, name, TARFS_MAX_NAME);
    n->name[TARFS_MAX_NAME] = '\0';
    return n;
}

/* Ensure all parent directories exist for a given path, returning the
 * deepest directory node.  Creates intermediate directories as needed. */
static tarfs_node_t *tarfs_ensure_path(tarfs_node_t *root, const char *path) {
    tarfs_node_t *cur = root;
    char buf[TARFS_MAX_NAME + 1];
    uint32_t i = 0;

    while (path[i] == '/') i++; /* skip leading slashes */

    while (path[i]) {
        uint32_t j = 0;
        while (path[i] && path[i] != '/' && j < TARFS_MAX_NAME) {
            buf[j++] = path[i++];
        }
        buf[j] = '\0';
        if (j == 0) {
            if (path[i] == '/') i++;
            continue;
        }

        tarfs_node_t *child = tarfs_find_child(cur, buf);
        if (!child) {
            /* Create intermediate directory */
            child = tarfs_new_node(buf, FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_EXEC);
            if (!child) return NULL;
            child->parent = cur;
            child->next_sibling = cur->child;
            cur->child = child;
        }
        cur = child;

        if (path[i] == '/') i++;
    }
    return cur;
}

/* ------------------------------------------------------------------ */
/* inode_ops                                                          */
/* ------------------------------------------------------------------ */

static dentry_t *tarfs_lookup_op(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode || !dir->inode->private_data || !name) return NULL;
    tarfs_node_t *d = (tarfs_node_t *)dir->inode->private_data;
    if (!(d->mode & FILE_MODE_DIR)) return NULL;
    if (strcmp(name, ".") == 0) return dir;
    if (strcmp(name, "..") == 0) return dir->parent;
    if (tarfs_find_child(d, name) == NULL) return NULL;
    return dir;
}

static int32_t tarfs_create_op(dentry_t *dir, const char *name, uint32_t mode) {
    (void)dir; (void)name; (void)mode;
    return -1; /* read-only */
}

static int32_t tarfs_mkdir_op(dentry_t *dir, const char *name, uint32_t mode) {
    (void)dir; (void)name; (void)mode;
    return -1; /* read-only */
}

static int32_t tarfs_unlink_op(dentry_t *dir, const char *name) {
    (void)dir; (void)name;
    return -1; /* read-only */
}

static int32_t tarfs_rename_op(dentry_t *old_dir, const char *old_name,
                               dentry_t *new_dir, const char *new_name) {
    (void)old_dir; (void)old_name; (void)new_dir; (void)new_name;
    return -1; /* read-only */
}

/* ------------------------------------------------------------------ */
/* file_ops                                                           */
/* ------------------------------------------------------------------ */

static int32_t tarfs_file_open(inode_t *inode, file_t *file) {
    (void)inode;
    file->offset = 0;
    return 0;
}

static int32_t tarfs_file_read(file_t *file, void *buf, uint32_t count) {
    if (!file || !file->inode || !file->inode->private_data) return -1;
    tarfs_node_t *n = (tarfs_node_t *)file->inode->private_data;
    if (n->mode & FILE_MODE_DIR) return -1;
    if (!tarfs_archive_ptr) return -1;
    if (file->offset >= n->size) return 0;

    uint32_t avail = n->size - file->offset;
    uint32_t to_copy = (count < avail) ? count : avail;
    memcpy(buf, tarfs_archive_ptr + n->data_offset + file->offset, to_copy);
    file->offset += to_copy;
    return (int32_t)to_copy;
}

static int32_t tarfs_file_close(file_t *file) {
    (void)file;
    return 0;
}

static int32_t tarfs_file_seek(file_t *file, int32_t offset, int32_t whence) {
    if (!file || !file->inode || !file->inode->private_data) return -1;
    tarfs_node_t *n = (tarfs_node_t *)file->inode->private_data;
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

/* ------------------------------------------------------------------ */
/* Build the VFS tree from the tar archive                            */
/* ------------------------------------------------------------------ */

static int32_t tarfs_build_tree(tarfs_node_t *root, const uint8_t *archive,
                                uint32_t archive_size) {
    uint32_t offset = 0;

    while (offset + sizeof(posix_header_t) <= archive_size) {
        const posix_header_t *hdr = (const posix_header_t *)(archive + offset);

        /* End of archive: two consecutive zero blocks */
        if (hdr->name[0] == '\0') break;

        /* Validate magic */
        if (memcmp(hdr->magic, "ustar", 5) != 0) {
            /* Skip this block if not a valid ustar header */
            offset += 512;
            continue;
        }

        uint32_t file_size = octal_to_int(hdr->size, 11);
        char typeflag = hdr->typeflag;

        /* Compute the data offset (right after the 512-byte header) */
        uint32_t data_off = offset + 512;

        /* Build the full name: prefix + name */
        char fullname[256];
        fullname[0] = '\0';
        if (hdr->prefix[0] != '\0') {
            uint32_t plen = 0;
            while (plen < 155 && hdr->prefix[plen] != '\0') plen++;
            memcpy(fullname, hdr->prefix, plen);
            fullname[plen] = '/';
            memcpy(fullname + plen + 1, hdr->name, 100);
            fullname[plen + 1 + 100] = '\0';
        } else {
            uint32_t nlen = 0;
            while (nlen < 100 && hdr->name[nlen] != '\0') nlen++;
            memcpy(fullname, hdr->name, nlen);
            fullname[nlen] = '\0';
        }

        /* Remove trailing slashes */
        uint32_t flen = strlen(fullname);
        while (flen > 1 && fullname[flen - 1] == '/') {
            fullname[--flen] = '\0';
        }

        /* Determine if this is a directory or a file */
        int is_dir = 0;
        if (typeflag == '5' || (typeflag == '\0' && flen > 0 && fullname[flen - 1] == '/')) {
            is_dir = 1;
        } else if (typeflag == '0' || typeflag == '\0') {
            is_dir = 0;
        } else {
            /* Skip links and other types */
            uint32_t blocks = (file_size + 511) / 512;
            offset += 512 + blocks * 512;
            continue;
        }

        /* Find or create the parent directory */
        /* Extract parent path */
        char parent_path[256];
        strncpy(parent_path, fullname, 255);
        parent_path[255] = '\0';

        char *last_slash = strrchr(parent_path, '/');
        const char *entry_name;
        if (last_slash) {
            *last_slash = '\0';
            entry_name = last_slash + 1;
            if (parent_path[0] == '\0') {
                parent_path[0] = '/';
                parent_path[1] = '\0';
            }
        } else {
            parent_path[0] = '\0';
            entry_name = fullname;
        }

        tarfs_node_t *parent;
        if (parent_path[0] == '\0') {
            parent = root;
        } else {
            parent = tarfs_ensure_path(root, parent_path);
        }

        if (!parent) {
            /* Could not create parent path, skip */
            uint32_t blocks = (file_size + 511) / 512;
            offset += 512 + blocks * 512;
            continue;
        }

        /* Skip if already exists (tar may list dir entries before their contents) */
        if (tarfs_find_child(parent, entry_name)) {
            /* If existing node is a dir and we're adding a file with the same
             * name as a directory entry, just skip */
            uint32_t blocks = (file_size + 511) / 512;
            offset += 512 + blocks * 512;
            continue;
        }

        /* Create the node */
        uint32_t mode;
        if (is_dir) {
            mode = FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_EXEC;
        } else {
            mode = FILE_MODE_REG | FILE_MODE_READ;
        }

        tarfs_node_t *node = tarfs_new_node(entry_name, mode);
        if (!node) return -1;

        node->parent = parent;
        node->next_sibling = parent->child;
        parent->child = node;

        if (is_dir) {
            node->size = 0;
            node->data_offset = 0;
            parent->nlinks++;
        } else {
            node->size = file_size;
            node->data_offset = data_off;
        }

        node->uid = octal_to_int(hdr->uid, 7);
        node->gid = octal_to_int(hdr->gid, 7);
        node->mtime = octal_to_int(hdr->mtime, 11);

        /* Advance past header + data blocks */
        uint32_t blocks = (file_size + 511) / 512;
        offset += 512 + blocks * 512;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Build VFS inodes/dentries from the tarfs tree                      */
/* ------------------------------------------------------------------ */

static void tarfs_build_vfs_tree(tarfs_node_t *tnode, inode_t *inode,
                                 superblock_t *sb, dentry_t *parent_dentry) {
    /* Create dentries for children */
    tarfs_node_t *child = tnode->child;
    while (child) {
        /* Create inode for child */
        inode_t *child_inode = (inode_t *)kmalloc(sizeof(inode_t));
        if (!child_inode) { child = child->next_sibling; continue; }
        memset(child_inode, 0, sizeof(inode_t));
        child_inode->ino  = child->ino;
        child_inode->mode = child->mode;
        child_inode->uid  = child->uid;
        child_inode->gid  = child->gid;
        child_inode->size = child->size;
        child_inode->nlinks = child->nlinks;
        child_inode->mtime = child->mtime;
        child_inode->sb   = sb;
        child_inode->ops  = &tarfs_inode_ops;
        child_inode->private_data = child;

        /* Create dentry for child */
        dentry_t *child_dentry = (dentry_t *)kmalloc(sizeof(dentry_t));
        if (!child_dentry) { kfree(child_inode); child = child->next_sibling; continue; }
        memset(child_dentry, 0, sizeof(dentry_t));
        strncpy(child_dentry->name, child->name, 255);
        child_dentry->name[255] = '\0';
        child_dentry->inode = child_inode;
        child_dentry->parent = parent_dentry;
        child_inode->dentries = child_dentry;

        /* Link into parent's child list */
        child_dentry->next_sibling = parent_dentry->child;
        parent_dentry->child = child_dentry;

        /* Recurse into directory children */
        if (child->mode & FILE_MODE_DIR) {
            tarfs_build_vfs_tree(child, child_inode, sb, child_dentry);
        }

        child = child->next_sibling;
    }
}

/* ------------------------------------------------------------------ */
/* mount / init                                                       */
/* ------------------------------------------------------------------ */

int32_t tarfs_mount(superblock_t *sb, void *data) {
    spinlock_init(&tarfs_lock);

    if (!data) return -1;
    tarfs_mount_info_t *info = (tarfs_mount_info_t *)data;
    if (!info->archive_data || info->archive_size == 0) return -1;

    tarfs_archive_ptr  = (const uint8_t *)info->archive_data;
    tarfs_archive_size = info->archive_size;

    /* Create the root node */
    tarfs_node_t *root = tarfs_new_node("/", FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_EXEC);
    if (!root) return -1;
    root->parent = root;

    /* Parse the tar archive and build the internal tree */
    spinlock_lock(&tarfs_lock);
    int32_t result = tarfs_build_tree(root, tarfs_archive_ptr, tarfs_archive_size);
    spinlock_unlock(&tarfs_lock);

    if (result != 0) {
        kfree(root);
        return -1;
    }

    /* Create the root inode */
    inode_t *root_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!root_inode) {
        kfree(root);
        return -1;
    }
    memset(root_inode, 0, sizeof(inode_t));
    root_inode->ino  = root->ino;
    root_inode->mode = root->mode;
    root_inode->size = 0;
    root_inode->nlinks = 2;
    root_inode->sb   = sb;
    root_inode->ops  = &tarfs_inode_ops;
    root_inode->private_data = root;

    /* Create the root dentry */
    dentry_t *root_dentry = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!root_dentry) {
        kfree(root_inode);
        kfree(root);
        return -1;
    }
    memset(root_dentry, 0, sizeof(dentry_t));
    strcpy(root_dentry->name, "/");
    root_dentry->inode = root_inode;
    root_dentry->parent = root_dentry; /* root's parent is itself */
    root_inode->dentries = root_dentry;

    /* Build VFS dentry tree from the tarfs tree */
    spinlock_lock(&tarfs_lock);
    tarfs_build_vfs_tree(root, root_inode, sb, root_dentry);
    spinlock_unlock(&tarfs_lock);

    sb->fs_type      = FS_TYPE_TARFS;
    sb->block_size   = 512;
    sb->total_blocks = tarfs_archive_size / 512;
    sb->free_blocks  = 0;
    sb->root         = root_inode;

    return 0;
}

int32_t tarfs_init(void) {
    return 0;
}
