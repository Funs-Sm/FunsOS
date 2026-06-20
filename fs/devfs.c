#include "devfs.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"
#include "vga_text.h"
#include "keyboard.h"
#include "io.h"

static devfs_device_t *device_list = NULL;

static int32_t devfs_open(inode_t *inode, file_t *file);
static int32_t devfs_read(file_t *file, void *buf, uint32_t count);
static int32_t devfs_write(file_t *file, const void *buf, uint32_t count);
static int32_t devfs_close(file_t *file);

file_ops_t devfs_file_ops = {
    .open = devfs_open,
    .read = devfs_read,
    .write = devfs_write,
    .close = devfs_close,
    .seek = NULL,
    .ioctl = NULL
};

static dentry_t *devfs_root_dentry;
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
    return 0;
}

int32_t devfs_init(void) {
    int32_t ret = vfs_mount("/dev", FS_TYPE_DEVFS, NULL);
    if (ret != 0) return ret;
    devfs_create_std_devices();
    return 0;
}

int32_t devfs_register(const char *name, uint32_t type, uint32_t major, uint32_t minor, file_ops_t *ops, void *data) {
    devfs_device_t *dev = (devfs_device_t *)kmalloc(sizeof(devfs_device_t));
    if (!dev) return -1;
    memset(dev, 0, sizeof(devfs_device_t));

    strncpy(dev->name, name, 31);
    dev->name[31] = '\0';
    dev->type = type;
    dev->major = major;
    dev->minor = minor;
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
            inode->mode = FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_WRITE;
        } else {
            inode->mode = FILE_MODE_REG | FILE_MODE_READ | FILE_MODE_WRITE;
        }
        inode->sb = devfs_root_inode->sb;
        inode->private_data = dev;
        inode->dentries = dentry;

        dentry->inode = inode;

        dentry->next_sibling = devfs_root_dentry->child;
        devfs_root_dentry->child = dentry;
    }

    return 0;
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
    uint8_t *src = (uint8_t *)file->offset;
    memcpy(buf, src, count);
    file->offset += count;
    return (int32_t)count;
}

static int32_t mem_write(file_t *file, const void *buf, uint32_t count) {
    uint8_t *dst = (uint8_t *)file->offset;
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

void devfs_create_std_devices(void) {
    devfs_register("null",    DEVICE_CHAR, 1, 3, &null_ops,    NULL);
    devfs_register("zero",    DEVICE_CHAR, 1, 5, &zero_ops,    NULL);
    devfs_register("console", DEVICE_CHAR, 5, 1, &console_ops, NULL);
    devfs_register("tty",     DEVICE_CHAR, 5, 0, &console_ops, NULL);
    devfs_register("kbd",     DEVICE_CHAR, 4, 0, &kbd_ops,     NULL);
    devfs_register("mem",     DEVICE_CHAR, 1, 1, &mem_ops,     NULL);
    devfs_register("port",    DEVICE_CHAR, 1, 4, &port_ops,    NULL);
}
