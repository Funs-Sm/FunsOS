#include "sysfs.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "stddef.h"
#include "version.h"

static dentry_t *sysfs_root_dentry;
static inode_t *sysfs_root_inode;
static superblock_t *sysfs_sb;

static file_ops_t sysfs_file_ops = {
    .read = sysfs_read,
    .write = sysfs_write,
    .open = NULL,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

static int32_t kernel_version_show(char *buf) {
    return sprintf(buf, KERNEL_NAME "\n");
}

static int32_t kernel_osrelease_show(char *buf) {
    return sprintf(buf, KERNEL_VERSION "\n");
}

static sysfs_attr_t kernel_version_attr = {
    .name = "version",
    .mode = FILE_MODE_READ,
    .show = kernel_version_show,
    .store = NULL
};

static sysfs_attr_t kernel_osrelease_attr = {
    .name = "osrelease",
    .mode = FILE_MODE_READ,
    .show = kernel_osrelease_show,
    .store = NULL
};

static inode_t *sysfs_create_inode(uint32_t mode, void *priv) {
    inode_t *in = (inode_t *)kmalloc(sizeof(inode_t));
    if (!in) return NULL;
    memset(in, 0, sizeof(inode_t));
    in->ino = (uint32_t)(uint32_t)in;
    in->mode = mode;
    in->sb = sysfs_sb;
    in->private_data = priv;
    in->ops = NULL;
    return in;
}

dentry_t *sysfs_create_dir(const char *name, dentry_t *parent) {
    dentry_t *de = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!de) return NULL;
    memset(de, 0, sizeof(dentry_t));
    strncpy(de->name, name, 255);
    de->name[255] = '\0';

    inode_t *in = sysfs_create_inode(FILE_MODE_READ | FILE_MODE_DIR, NULL);
    if (!in) {
        kfree(de);
        return NULL;
    }
    de->inode = in;

    if (!parent) parent = sysfs_root_dentry;
    de->parent = parent;
    de->next_sibling = parent->child;
    parent->child = de;

    return de;
}

dentry_t *sysfs_create_file(const char *name, dentry_t *parent, sysfs_attr_t *attr) {
    dentry_t *de = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!de) return NULL;
    memset(de, 0, sizeof(dentry_t));
    strncpy(de->name, name, 255);
    de->name[255] = '\0';

    inode_t *in = sysfs_create_inode(attr ? attr->mode : FILE_MODE_READ, attr);
    if (!in) {
        kfree(de);
        return NULL;
    }
    de->inode = in;

    if (!parent) parent = sysfs_root_dentry;
    de->parent = parent;
    de->next_sibling = parent->child;
    parent->child = de;

    return de;
}

int32_t sysfs_read(file_t *file, void *buf, uint32_t count) {
    if (!file || !buf || !file->inode) return -1;
    sysfs_attr_t *attr = (sysfs_attr_t *)file->inode->private_data;
    if (!attr || !attr->show) return -1;

    char tmp[256];
    int32_t len = attr->show(tmp);
    if (len < 0) return -1;

    if (file->offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - file->offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + file->offset, avail);
    file->offset += avail;
    return (int32_t)avail;
}

int32_t sysfs_write(file_t *file, const void *buf, uint32_t count) {
    if (!file || !buf || !file->inode) return -1;
    sysfs_attr_t *attr = (sysfs_attr_t *)file->inode->private_data;
    if (!attr || !attr->store) return -1;
    return attr->store((const char *)buf, count);
}

int32_t sysfs_mount(superblock_t *sb, void *data) {
    (void)data;

    sysfs_root_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!sysfs_root_inode) return -1;
    memset(sysfs_root_inode, 0, sizeof(inode_t));
    sysfs_root_inode->ino = 0;
    sysfs_root_inode->mode = FILE_MODE_READ | FILE_MODE_DIR;
    sysfs_root_inode->sb = sb;

    sysfs_root_dentry = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!sysfs_root_dentry) {
        kfree(sysfs_root_inode);
        return -1;
    }
    memset(sysfs_root_dentry, 0, sizeof(dentry_t));
    strcpy(sysfs_root_dentry->name, "/");
    sysfs_root_dentry->inode = sysfs_root_inode;

    sb->root = sysfs_root_inode;
    sb->fs_type = 5;
    sb->block_size = 4096;
    sysfs_sb = sb;

    return 0;
}

int32_t sysfs_init(void) {
    dentry_t *kernel_dir = sysfs_create_dir("kernel", NULL);
    if (!kernel_dir) return -1;

    sysfs_create_file("version", kernel_dir, &kernel_version_attr);
    sysfs_create_file("osrelease", kernel_dir, &kernel_osrelease_attr);

    dentry_t *devices_dir = sysfs_create_dir("devices", NULL);
    if (!devices_dir) return -1;

    dentry_t *bus_dir = sysfs_create_dir("bus", NULL);
    if (!bus_dir) return -1;

    dentry_t *class_dir = sysfs_create_dir("class", NULL);
    if (!class_dir) return -1;

    return 0;
}
