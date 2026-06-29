#include "sysfs.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "stddef.h"
#include "version.h"
#include "klog.h"

static dentry_t *sysfs_root_dentry;
static inode_t *sysfs_root_inode;
static superblock_t *sysfs_sb;

static char sys_hostname[64] = "funsos";

static file_ops_t sysfs_file_ops = {
    .read = sysfs_read,
    .write = sysfs_write,
    .open = NULL,
    .close = NULL,
    .seek = NULL,
    .ioctl = NULL
};

/* ---- /sys/kernel/ entries ---- */

static int32_t kernel_version_show(char *buf) {
    return sprintf(buf, KERNEL_NAME "\n");
}

static int32_t kernel_osrelease_show(char *buf) {
    return sprintf(buf, KERNEL_VERSION "\n");
}

static int32_t kernel_ostype_show(char *buf) {
    return sprintf(buf, OS_NAME "\n");
}

static int32_t kernel_hostname_show(char *buf) {
    return sprintf(buf, "%s\n", sys_hostname);
}

static int32_t kernel_hostname_store(const char *buf, uint32_t count) {
    if (count >= sizeof(sys_hostname)) count = sizeof(sys_hostname) - 1;
    strncpy(sys_hostname, buf, count);
    sys_hostname[count] = '\0';
    while (count > 0 && (sys_hostname[count-1] == '\n' || sys_hostname[count-1] == '\r')) {
        sys_hostname[--count] = '\0';
    }
    klog_info("sysfs: hostname changed to '%s'", sys_hostname);
    return (int32_t)count;
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

static sysfs_attr_t kernel_ostype_attr = {
    .name = "ostype",
    .mode = FILE_MODE_READ,
    .show = kernel_ostype_show,
    .store = NULL
};

static sysfs_attr_t kernel_hostname_attr = {
    .name = "hostname",
    .mode = FILE_MODE_READ | FILE_MODE_WRITE,
    .show = kernel_hostname_show,
    .store = kernel_hostname_store
};

/* ---- /sys/devices/ entries ---- */

static int32_t devices_system_show(char *buf) {
    return sprintf(buf, "system\n");
}

static sysfs_attr_t devices_system_attr = {
    .name = "system",
    .mode = FILE_MODE_READ,
    .show = devices_system_show,
    .store = NULL
};

/* ---- /sys/bus/ entries ---- */

static int32_t bus_pci_show(char *buf) {
    return sprintf(buf, "pci\n");
}

static int32_t bus_usb_show(char *buf) {
    return sprintf(buf, "usb\n");
}

static sysfs_attr_t bus_pci_attr = {
    .name = "pci",
    .mode = FILE_MODE_READ,
    .show = bus_pci_show,
    .store = NULL
};

static sysfs_attr_t bus_usb_attr = {
    .name = "usb",
    .mode = FILE_MODE_READ,
    .show = bus_usb_show,
    .store = NULL
};

/* ---- /sys/class/ entries ---- */

static int32_t class_net_show(char *buf) {
    return sprintf(buf, "net\n");
}

static int32_t class_block_show(char *buf) {
    return sprintf(buf, "block\n");
}

static int32_t class_input_show(char *buf) {
    return sprintf(buf, "input\n");
}

static int32_t class_graphics_show(char *buf) {
    return sprintf(buf, "graphics\n");
}

static sysfs_attr_t class_net_attr = {
    .name = "net",
    .mode = FILE_MODE_READ,
    .show = class_net_show,
    .store = NULL
};

static sysfs_attr_t class_block_attr = {
    .name = "block",
    .mode = FILE_MODE_READ,
    .show = class_block_show,
    .store = NULL
};

static sysfs_attr_t class_input_attr = {
    .name = "input",
    .mode = FILE_MODE_READ,
    .show = class_input_show,
    .store = NULL
};

static sysfs_attr_t class_graphics_attr = {
    .name = "graphics",
    .mode = FILE_MODE_READ,
    .show = class_graphics_show,
    .store = NULL
};

/* ---- /sys/power/ entries ---- */

static int32_t power_state_show(char *buf) {
    return sprintf(buf, "on\n");
}

static int32_t power_state_store(const char *buf, uint32_t count) {
    klog_info("sysfs: power state change requested");
    return (int32_t)count;
}

static int32_t power_wakeup_show(char *buf) {
    return sprintf(buf, "enabled\n");
}

static sysfs_attr_t power_state_attr = {
    .name = "state",
    .mode = FILE_MODE_READ | FILE_MODE_WRITE,
    .show = power_state_show,
    .store = power_state_store
};

static sysfs_attr_t power_wakeup_attr = {
    .name = "wakeup",
    .mode = FILE_MODE_READ,
    .show = power_wakeup_show,
    .store = NULL
};

/* ---- core functions ---- */

static inode_t *sysfs_create_inode(uint32_t mode, void *priv) {
    inode_t *in = (inode_t *)kmalloc(sizeof(inode_t));
    if (!in) return NULL;
    memset(in, 0, sizeof(inode_t));
    in->ino = (uint32_t)(uintptr_t)in;
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

    klog_info("sysfs: created directory /sys/%s", name);
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

    char tmp[512];
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
    sb->fs_type = FS_TYPE_SYSFS;
    sb->block_size = 4096;
    sysfs_sb = sb;

    klog_info("sysfs: mounted successfully");
    return 0;
}

int32_t sysfs_init(void) {
    klog_info("sysfs: initializing sysfs entries");

    /* /sys/kernel/ */
    dentry_t *kernel_dir = sysfs_create_dir("kernel", NULL);
    if (!kernel_dir) {
        klog_err("sysfs: failed to create /sys/kernel");
        return -1;
    }
    sysfs_create_file("version", kernel_dir, &kernel_version_attr);
    sysfs_create_file("osrelease", kernel_dir, &kernel_osrelease_attr);
    sysfs_create_file("ostype", kernel_dir, &kernel_ostype_attr);
    sysfs_create_file("hostname", kernel_dir, &kernel_hostname_attr);
    klog_info("sysfs: /sys/kernel/ entries created");

    /* /sys/devices/ */
    dentry_t *devices_dir = sysfs_create_dir("devices", NULL);
    if (!devices_dir) {
        klog_err("sysfs: failed to create /sys/devices");
        return -1;
    }
    dentry_t *devices_system_dir = sysfs_create_dir("system", devices_dir);
    if (devices_system_dir) {
        sysfs_create_file("system", devices_system_dir, &devices_system_attr);
    }
    klog_info("sysfs: /sys/devices/ entries created");

    /* /sys/bus/ */
    dentry_t *bus_dir = sysfs_create_dir("bus", NULL);
    if (!bus_dir) {
        klog_err("sysfs: failed to create /sys/bus");
        return -1;
    }
    dentry_t *bus_pci_dir = sysfs_create_dir("pci", bus_dir);
    if (bus_pci_dir) {
        sysfs_create_file("pci", bus_pci_dir, &bus_pci_attr);
    }
    dentry_t *bus_usb_dir = sysfs_create_dir("usb", bus_dir);
    if (bus_usb_dir) {
        sysfs_create_file("usb", bus_usb_dir, &bus_usb_attr);
    }
    klog_info("sysfs: /sys/bus/ entries created");

    /* /sys/class/ */
    dentry_t *class_dir = sysfs_create_dir("class", NULL);
    if (!class_dir) {
        klog_err("sysfs: failed to create /sys/class");
        return -1;
    }
    dentry_t *class_net_dir = sysfs_create_dir("net", class_dir);
    if (class_net_dir) {
        sysfs_create_file("net", class_net_dir, &class_net_attr);
    }
    dentry_t *class_block_dir = sysfs_create_dir("block", class_dir);
    if (class_block_dir) {
        sysfs_create_file("block", class_block_dir, &class_block_attr);
    }
    dentry_t *class_input_dir = sysfs_create_dir("input", class_dir);
    if (class_input_dir) {
        sysfs_create_file("input", class_input_dir, &class_input_attr);
    }
    dentry_t *class_graphics_dir = sysfs_create_dir("graphics", class_dir);
    if (class_graphics_dir) {
        sysfs_create_file("graphics", class_graphics_dir, &class_graphics_attr);
    }
    klog_info("sysfs: /sys/class/ entries created");

    /* /sys/power/ */
    dentry_t *power_dir = sysfs_create_dir("power", NULL);
    if (!power_dir) {
        klog_err("sysfs: failed to create /sys/power");
        return -1;
    }
    sysfs_create_file("state", power_dir, &power_state_attr);
    sysfs_create_file("wakeup", power_dir, &power_wakeup_attr);
    klog_info("sysfs: /sys/power/ entries created");

    klog_info("sysfs: initialization complete");
    return 0;
}
