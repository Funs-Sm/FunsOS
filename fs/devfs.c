#include "devfs.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"
#include "vga_text.h"
#include "keyboard.h"
#include "io.h"
#include "rtc.h"
#include "klog.h"

static devfs_device_t *device_list = NULL;

static int32_t devfs_open(inode_t *inode, file_t *file);
static int32_t devfs_read(file_t *file, void *buf, uint32_t count);
static int32_t devfs_write(file_t *file, const void *buf, uint32_t count);
static int32_t devfs_close(file_t *file);
static int32_t devfs_seek(file_t *file, int32_t offset, int32_t whence);
static int32_t devfs_ioctl(file_t *file, uint32_t cmd, void *arg);

file_ops_t devfs_file_ops = {
    .open = devfs_open,
    .read = devfs_read,
    .write = devfs_write,
    .close = devfs_close,
    .seek = devfs_seek,
    .ioctl = devfs_ioctl
};

dentry_t *devfs_root_dentry;
static inode_t *devfs_root_inode;

int32_t devfs_mount_internal(superblock_t *sb, void *data) {
    (void)data;

    devfs_root_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!devfs_root_inode) return -1;
    memset(devfs_root_inode, 0, sizeof(inode_t));
    devfs_root_inode->ino = 0;
    devfs_root_inode->mode = FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_WRITE;
    devfs_root_inode->nlinks = 1;
    devfs_root_inode->sb = sb;

    devfs_root_dentry = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!devfs_root_dentry) {
        kfree(devfs_root_inode);
        return -1;
    }
    memset(devfs_root_dentry, 0, sizeof(dentry_t));
    devfs_root_dentry->name[0] = '/';
    devfs_root_dentry->inode = devfs_root_inode;
    devfs_root_dentry->parent = devfs_root_dentry;
    devfs_root_inode->dentries = devfs_root_dentry;

    sb->root = devfs_root_inode;
    sb->fs_type = FS_TYPE_DEVFS;
    klog_info("devfs: mounted successfully");
    return 0;
}

int32_t devfs_init(void) {
    int32_t ret = vfs_mount("/dev", FS_TYPE_DEVFS, NULL);
    if (ret != 0) {
        klog_err("devfs: failed to mount, ret=%d", ret);
        return ret;
    }
    klog_info("devfs: initializing standard devices");
    devfs_create_std_devices();
    klog_info("devfs: initialization complete");
    return 0;
}

int32_t devfs_register_with_perm(const char *name, uint32_t type, uint32_t major, uint32_t minor,
                                 file_ops_t *ops, void *data, uint32_t mode, uint32_t uid, uint32_t gid) {
    devfs_device_t *dev = (devfs_device_t *)kmalloc(sizeof(devfs_device_t));
    if (!dev) return -1;
    memset(dev, 0, sizeof(devfs_device_t));

    strncpy(dev->name, name, 31);
    dev->name[31] = '\0';
    dev->type = type;
    dev->major = major;
    dev->minor = minor;
    dev->mode = mode;
    dev->uid = uid;
    dev->gid = gid;
    dev->ops = ops;
    dev->private_data = data;

    dev->next = device_list;
    device_list = dev;

    if (devfs_root_dentry) {
        dentry_t *dentry = (dentry_t *)kmalloc(sizeof(dentry_t));
        if (!dentry) {
            return 0;
        }
        memset(dentry, 0, sizeof(dentry_t));
        strncpy(dentry->name, name, 255);
        dentry->name[255] = '\0';
        dentry->parent = devfs_root_dentry;

        inode_t *inode = (inode_t *)kmalloc(sizeof(inode_t));
        if (!inode) {
            kfree(dentry);
            return 0;
        }
        memset(inode, 0, sizeof(inode_t));
        inode->ino = (major << 8) | minor;
        if (type == DEVICE_BLOCK) {
            inode->mode = FILE_MODE_REG;
        } else {
            inode->mode = FILE_MODE_REG;
        }
        if (mode & 0444) inode->mode |= FILE_MODE_READ;
        if (mode & 0222) inode->mode |= FILE_MODE_WRITE;
        if (mode & 0111) inode->mode |= FILE_MODE_EXEC;
        inode->uid = uid;
        inode->gid = gid;
        inode->sb = devfs_root_inode->sb;
        inode->private_data = dev;
        inode->dentries = dentry;

        dentry->inode = inode;

        dentry->next_sibling = devfs_root_dentry->child;
        devfs_root_dentry->child = dentry;
    }

    klog_info("devfs: registered device %s (%u,%u)", name, major, minor);
    return 0;
}

int32_t devfs_register(const char *name, uint32_t type, uint32_t major, uint32_t minor, file_ops_t *ops, void *data) {
    return devfs_register_with_perm(name, type, major, minor, ops, data, DEV_PERM_RW, 0, 0);
}

int32_t devfs_unregister(const char *name) {
    devfs_device_t *prev = NULL;
    devfs_device_t *curr = device_list;

    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            if (prev) {
                prev->next = curr->next;
            } else {
                device_list = curr->next;
            }

            if (devfs_root_dentry) {
                dentry_t *dprev = NULL;
                dentry_t *dcurr = devfs_root_dentry->child;
                while (dcurr) {
                    if (strcmp(dcurr->name, name) == 0) {
                        if (dprev) {
                            dprev->next_sibling = dcurr->next_sibling;
                        } else {
                            devfs_root_dentry->child = dcurr->next_sibling;
                        }
                        if (dcurr->inode) kfree(dcurr->inode);
                        kfree(dcurr);
                        break;
                    }
                    dprev = dcurr;
                    dcurr = dcurr->next_sibling;
                }
            }

            klog_info("devfs: unregistered device %s", name);
            kfree(curr);
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }

    return -1;
}

devfs_device_t *devfs_find(const char *name) {
    devfs_device_t *curr = device_list;
    while (curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

static int32_t devfs_open(inode_t *inode, file_t *file) {
    devfs_device_t *dev = (devfs_device_t *)inode->private_data;
    if (!dev) return -1;
    file->private_data = dev;
    if (dev->ops && dev->ops->open) {
        return dev->ops->open(inode, file);
    }
    return 0;
}

static int32_t devfs_read(file_t *file, void *buf, uint32_t count) {
    devfs_device_t *dev = (devfs_device_t *)file->private_data;
    if (!dev || !dev->ops || !dev->ops->read) return -1;
    return dev->ops->read(file, buf, count);
}

static int32_t devfs_write(file_t *file, const void *buf, uint32_t count) {
    devfs_device_t *dev = (devfs_device_t *)file->private_data;
    if (!dev || !dev->ops || !dev->ops->write) return -1;
    return dev->ops->write(file, buf, count);
}

static int32_t devfs_close(file_t *file) {
    devfs_device_t *dev = (devfs_device_t *)file->private_data;
    if (dev && dev->ops && dev->ops->close) {
        return dev->ops->close(file);
    }
    return 0;
}

static int32_t devfs_seek(file_t *file, int32_t offset, int32_t whence) {
    devfs_device_t *dev = (devfs_device_t *)file->private_data;
    if (dev && dev->ops && dev->ops->seek) {
        return dev->ops->seek(file, offset, whence);
    }
    if (!file->inode || (file->inode->mode & FILE_MODE_DIR)) return -ENOTDIR;
    switch (whence) {
        case SEEK_SET: file->offset = (uint32_t)offset; break;
        case SEEK_CUR: file->offset += offset; break;
        case SEEK_END: file->offset = file->inode->size + offset; break;
        default: return -EINVAL;
    }
    return (int32_t)file->offset;
}

static int32_t devfs_ioctl(file_t *file, uint32_t cmd, void *arg) {
    devfs_device_t *dev = (devfs_device_t *)file->private_data;
    if (dev && dev->ops && dev->ops->ioctl) {
        return dev->ops->ioctl(file, cmd, arg);
    }
    return -ENOSYS;
}

/* ------------------------------------------------------------------ */
/*  Standard device node implementations                               */
/* ------------------------------------------------------------------ */

/* /dev/null - Read returns 0, write discards */
static int32_t null_read(file_t *file, void *buf, uint32_t count) {
    (void)file;
    (void)buf;
    (void)count;
    return 0;
}

static int32_t null_write(file_t *file, const void *buf, uint32_t count) {
    (void)file;
    (void)buf;
    return (int32_t)count;
}

static file_ops_t null_ops = {
    .open = NULL,
    .read = null_read,
    .write = null_write,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

/* /dev/zero - Read returns zeros, write discards */
static int32_t zero_read(file_t *file, void *buf, uint32_t count) {
    (void)file;
    memset(buf, 0, count);
    return (int32_t)count;
}

static int32_t zero_write(file_t *file, const void *buf, uint32_t count) {
    (void)file;
    (void)buf;
    return (int32_t)count;
}

static file_ops_t zero_ops = {
    .open = NULL,
    .read = zero_read,
    .write = zero_write,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

/* /dev/full - Read returns zeros, write returns ENOSPC */
static int32_t full_read(file_t *file, void *buf, uint32_t count) {
    return zero_read(file, buf, count);
}

static int32_t full_write(file_t *file, const void *buf, uint32_t count) {
    (void)file;
    (void)buf;
    (void)count;
    return -ENOSPC;
}

static file_ops_t full_ops = {
    .open = NULL,
    .read = full_read,
    .write = full_write,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

/* /dev/console and /dev/tty - Write to VGA text mode */
static int32_t console_read(file_t *file, void *buf, uint32_t count) {
    (void)file;
    (void)buf;
    (void)count;
    return 0;
}

static int32_t console_write(file_t *file, const void *buf, uint32_t count) {
    (void)file;
    const char *str = (const char *)buf;
    uint32_t i;
    for (i = 0; i < count; i++) {
        vga_text_putchar(str[i]);
    }
    return (int32_t)count;
}

static file_ops_t console_ops = {
    .open = NULL,
    .read = console_read,
    .write = console_write,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

/* /dev/kbd - Read from keyboard buffer */
static int32_t kbd_read(file_t *file, void *buf, uint32_t count) {
    (void)file;
    if (!buf || count == 0) return 0;

    char *out = (char *)buf;
    uint32_t i = 0;
    while (i < count && keyboard_has_data()) {
        keyboard_event_t event;
        if (!keyboard_get_event(&event)) break;
        if ((event.flags & KEY_PRESSED) && event.ascii) {
            out[i] = event.ascii;
            i++;
        }
    }
    return (int32_t)i;
}

static int32_t kbd_write(file_t *file, const void *buf, uint32_t count) {
    (void)file;
    (void)buf;
    return (int32_t)count;
}

static file_ops_t kbd_ops = {
    .open = NULL,
    .read = kbd_read,
    .write = kbd_write,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

/* /dev/mem - Physical memory access */
static int32_t mem_read(file_t *file, void *buf, uint32_t count) {
    uint8_t *src = (uint8_t *)(uintptr_t)file->offset;
    memcpy(buf, src, count);
    file->offset += count;
    return (int32_t)count;
}

static int32_t mem_write(file_t *file, const void *buf, uint32_t count) {
    uint8_t *dst = (uint8_t *)(uintptr_t)file->offset;
    memcpy(dst, buf, count);
    file->offset += count;
    return (int32_t)count;
}

static file_ops_t mem_ops = {
    .open = NULL,
    .read = mem_read,
    .write = mem_write,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

/* /dev/kmem - Kernel virtual memory access (same as mem for now) */
static int32_t kmem_read(file_t *file, void *buf, uint32_t count) {
    return mem_read(file, buf, count);
}

static int32_t kmem_write(file_t *file, const void *buf, uint32_t count) {
    return mem_write(file, buf, count);
}

static file_ops_t kmem_ops = {
    .open = NULL,
    .read = kmem_read,
    .write = kmem_write,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

/* /dev/port - I/O port access */
static int32_t port_read(file_t *file, void *buf, uint32_t count) {
    uint16_t port = (uint16_t)file->offset;
    uint8_t *out = (uint8_t *)buf;
    uint32_t i;
    for (i = 0; i < count; i++) {
        out[i] = inb(port + i);
    }
    file->offset += count;
    return (int32_t)count;
}

static int32_t port_write(file_t *file, const void *buf, uint32_t count) {
    uint16_t port = (uint16_t)file->offset;
    const uint8_t *data = (const uint8_t *)buf;
    uint32_t i;
    for (i = 0; i < count; i++) {
        outb(port + i, data[i]);
    }
    file->offset += count;
    return (int32_t)count;
}

static file_ops_t port_ops = {
    .open = NULL,
    .read = port_read,
    .write = port_write,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

/* Simple LCG pseudo-random number generator for /dev/random and /dev/urandom */
static uint32_t random_seed = 0;

static uint32_t dev_random_next(void) {
    if (random_seed == 0) {
        random_seed = rtc_get_timestamp() ^ 0xDEADBEEF;
    }
    random_seed = random_seed * 1103515245 + 12345;
    return random_seed;
}

/* /dev/random - Returns pseudo-random bytes */
static int32_t random_read(file_t *file, void *buf, uint32_t count) {
    (void)file;
    uint8_t *out = (uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) {
        if (i % 4 == 0) {
            uint32_t r = dev_random_next();
            out[i] = (uint8_t)(r & 0xFF);
        } else {
            uint32_t r = dev_random_next();
            out[i] = (uint8_t)((r >> ((i % 4) * 8)) & 0xFF);
        }
    }
    return (int32_t)count;
}

static int32_t random_write(file_t *file, const void *buf, uint32_t count) {
    (void)file;
    (void)buf;
    if (count > 0) {
        const uint8_t *p = (const uint8_t *)buf;
        random_seed ^= p[0];
        for (uint32_t i = 1; i < count; i++) {
            random_seed = random_seed * 31 + p[i];
        }
    }
    return (int32_t)count;
}

static file_ops_t random_ops = {
    .open = NULL,
    .read = random_read,
    .write = random_write,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

/* /dev/urandom - Same as /dev/random for now (non-blocking) */
static int32_t urandom_read(file_t *file, void *buf, uint32_t count) {
    return random_read(file, buf, count);
}

static int32_t urandom_write(file_t *file, const void *buf, uint32_t count) {
    return random_write(file, buf, count);
}

static file_ops_t urandom_ops = {
    .open = NULL,
    .read = urandom_read,
    .write = urandom_write,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

/* /dev/sda, /dev/sda1, etc. - Disk device stubs */
static int32_t disk_read(file_t *file, void *buf, uint32_t count) {
    (void)file;
    (void)buf;
    (void)count;
    return 0;
}

static int32_t disk_write(file_t *file, const void *buf, uint32_t count) {
    (void)file;
    (void)buf;
    return (int32_t)count;
}

static file_ops_t disk_ops = {
    .open = NULL,
    .read = disk_read,
    .write = disk_write,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

void devfs_create_std_devices(void) {
    klog_info("devfs: creating standard character devices");

    devfs_register_with_perm("null",    DEVICE_CHAR, 1, 3, &null_ops,    NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("zero",    DEVICE_CHAR, 1, 5, &zero_ops,    NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("full",    DEVICE_CHAR, 1, 7, &full_ops,    NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("console", DEVICE_CHAR, 5, 1, &console_ops, NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("tty",     DEVICE_CHAR, 5, 0, &console_ops, NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("kbd",     DEVICE_CHAR, 4, 0, &kbd_ops,     NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("mem",     DEVICE_CHAR, 1, 1, &mem_ops,     NULL, DEV_PERM_ROOT_RW, 0, 0);
    devfs_register_with_perm("kmem",    DEVICE_CHAR, 1, 2, &kmem_ops,    NULL, DEV_PERM_ROOT_RW, 0, 0);
    devfs_register_with_perm("port",    DEVICE_CHAR, 1, 4, &port_ops,    NULL, DEV_PERM_ROOT_RW, 0, 0);
    devfs_register_with_perm("random",  DEVICE_CHAR, 1, 8, &random_ops,  NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("urandom", DEVICE_CHAR, 1, 9, &urandom_ops, NULL, DEV_PERM_RW, 0, 0);

    klog_info("devfs: creating standard block devices");
    devfs_register_with_perm("sda",     DEVICE_BLOCK, 8, 0, &disk_ops,   NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("sda1",    DEVICE_BLOCK, 8, 1, &disk_ops,   NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("sda2",    DEVICE_BLOCK, 8, 2, &disk_ops,   NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("sda3",    DEVICE_BLOCK, 8, 3, &disk_ops,   NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("sda4",    DEVICE_BLOCK, 8, 4, &disk_ops,   NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("sdb",     DEVICE_BLOCK, 8, 16, &disk_ops,  NULL, DEV_PERM_RW, 0, 0);
    devfs_register_with_perm("sdb1",    DEVICE_BLOCK, 8, 17, &disk_ops,  NULL, DEV_PERM_RW, 0, 0);

    klog_info("devfs: standard devices created");
}
