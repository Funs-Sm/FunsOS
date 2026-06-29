#include "loopdev.h"
#include "klog.h"
#include "devfs.h"
#include "vfs.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"

static loopdev_t loop_devices[LOOPDEV_MAX_DEVICES];
static int loopdev_initialized = 0;

static int32_t loopdev_dev_read(file_t *file, void *buf, uint32_t count) {
    int id;
    loopdev_t *dev;
    devfs_device_t *ddev;

    if (!file) return -EINVAL;

    ddev = (devfs_device_t *)file->private_data;
    if (!ddev) return -ENODEV;
    id = (int)ddev->minor;

    dev = loopdev_get(id);
    if (!dev || !dev->in_use) return -ENODEV;

    int32_t ret = loopdev_read(id, file->offset, count, buf);
    if (ret < 0) return -EIO;

    file->offset += (uint32_t)ret;
    return ret;
}

static int32_t loopdev_dev_write(file_t *file, const void *buf, uint32_t count) {
    int id;
    loopdev_t *dev;
    devfs_device_t *ddev;

    if (!file) return -EINVAL;

    ddev = (devfs_device_t *)file->private_data;
    if (!ddev) return -ENODEV;
    id = (int)ddev->minor;

    dev = loopdev_get(id);
    if (!dev || !dev->in_use) return -ENODEV;

    int32_t ret = loopdev_write(id, file->offset, count, buf);
    if (ret < 0) return -EIO;

    file->offset += (uint32_t)ret;
    return ret;
}

static int32_t loopdev_dev_open(inode_t *inode, file_t *file) {
    int id;
    loopdev_t *dev;
    devfs_device_t *ddev;

    if (!inode || !file) return -EINVAL;

    ddev = (devfs_device_t *)inode->private_data;
    if (!ddev) return -ENODEV;
    id = (int)ddev->minor;

    dev = loopdev_get(id);
    if (!dev) return -ENODEV;

    dev->ref_count++;
    file->private_data = ddev;
    return 0;
}

static int32_t loopdev_dev_close(file_t *file) {
    int id;
    loopdev_t *dev;
    devfs_device_t *ddev;

    if (!file) return -EINVAL;

    ddev = (devfs_device_t *)file->private_data;
    if (!ddev) return -ENODEV;
    id = (int)ddev->minor;

    dev = loopdev_get(id);
    if (!dev) return -ENODEV;

    if (dev->ref_count > 0) {
        dev->ref_count--;
    }
    return 0;
}

static int32_t loopdev_dev_seek(file_t *file, int32_t offset, int32_t whence) {
    int id;
    loopdev_t *dev;
    devfs_device_t *ddev;
    uint32_t new_offset;

    if (!file) return -EINVAL;

    ddev = (devfs_device_t *)file->private_data;
    if (!ddev) return -ENODEV;
    id = (int)ddev->minor;

    dev = loopdev_get(id);
    if (!dev || !dev->in_use) return -ENODEV;

    switch (whence) {
    case SEEK_SET:
        new_offset = (offset >= 0) ? (uint32_t)offset : 0;
        break;
    case SEEK_CUR:
        new_offset = file->offset + (uint32_t)((offset >= 0) ? offset : 0);
        break;
    case SEEK_END:
        new_offset = dev->file_size + (uint32_t)((offset >= 0) ? offset : 0);
        break;
    default:
        return -EINVAL;
    }

    if (new_offset > dev->file_size) {
        new_offset = dev->file_size;
    }

    file->offset = new_offset;
    return (int32_t)new_offset;
}

static file_ops_t loopdev_file_ops = {
    .open = loopdev_dev_open,
    .read = loopdev_dev_read,
    .write = loopdev_dev_write,
    .close = loopdev_dev_close,
    .seek = loopdev_dev_seek,
    .ioctl = NULL
};

loopdev_t *loopdev_get(int id) {
    if (id < 0 || id >= LOOPDEV_MAX_DEVICES) return NULL;
    return &loop_devices[id];
}

int32_t loopdev_read(int id, uint32_t offset, uint32_t count, void *buf) {
    loopdev_t *dev = loopdev_get(id);
    if (!dev || !dev->in_use || !buf) return -EINVAL;
    if (offset >= dev->file_size) return 0;
    if (offset + count > dev->file_size) {
        count = dev->file_size - offset;
    }
    if (count == 0) return 0;

    file_t *f = dev->file;
    if (!f) return -EIO;

    uint32_t old_offset = f->offset;
    f->offset = offset;
    int32_t ret = vfs_read(f, buf, count);
    f->offset = old_offset;
    return ret;
}

int32_t loopdev_write(int id, uint32_t offset, uint32_t count, const void *buf) {
    loopdev_t *dev = loopdev_get(id);
    if (!dev || !dev->in_use || !buf) return -EINVAL;
    if (offset > dev->file_size) return -EINVAL;

    file_t *f = dev->file;
    if (!f) return -EIO;

    uint32_t old_offset = f->offset;
    f->offset = offset;
    int32_t ret = vfs_write(f, buf, count);
    if (ret > 0 && offset + (uint32_t)ret > dev->file_size) {
        dev->file_size = offset + (uint32_t)ret;
    }
    f->offset = old_offset;
    return ret;
}

int loopdev_setup(const char *path) {
    if (!path || !*path) return -EINVAL;
    if (!loopdev_initialized) return -ENODEV;

    for (int i = 0; i < LOOPDEV_MAX_DEVICES; i++) {
        if (!loop_devices[i].in_use) {
            file_t *f = NULL;
            int32_t ret = vfs_open(path, FILE_MODE_READ | FILE_MODE_WRITE | FILE_MODE_REG, &f);
            if (ret != 0 || !f) {
                return -ENOENT;
            }

            inode_t st;
            memset(&st, 0, sizeof(st));
            ret = vfs_stat(path, &st);
            if (ret != 0) {
                vfs_close(f);
                return -EIO;
            }

            loop_devices[i].in_use = 1;
            loop_devices[i].file = f;
            loop_devices[i].file_size = st.size;
            strncpy(loop_devices[i].path, path, sizeof(loop_devices[i].path) - 1);
            loop_devices[i].path[sizeof(loop_devices[i].path) - 1] = '\0';

            klog_info("loopdev: loop%d setup with %s (%u bytes)\n", i, path, st.size);
            return i;
        }
    }

    return -ENOSPC;
}

int loopdev_detach(int id) {
    loopdev_t *dev = loopdev_get(id);
    if (!dev) return -ENODEV;
    if (!dev->in_use) return -ENODEV;
    if (dev->ref_count > 0) return -EBUSY;

    if (dev->file) {
        vfs_close(dev->file);
        dev->file = NULL;
    }

    dev->in_use = 0;
    dev->file_size = 0;
    dev->ref_count = 0;
    dev->path[0] = '\0';

    klog_info("loopdev: loop%d detached\n", id);
    return 0;
}

void loopdev_init(void) {
    if (loopdev_initialized) return;

    memset(loop_devices, 0, sizeof(loop_devices));

    char dev_name[32];
    for (int i = 0; i < LOOPDEV_MAX_DEVICES; i++) {
        snprintf(dev_name, sizeof(dev_name), "loop%d", i);
        devfs_register(dev_name, DEVICE_BLOCK, 7, (uint32_t)i, &loopdev_file_ops, NULL);
    }

    loopdev_initialized = 1;
    klog_info("loopdev: initialized with %d loop devices\n", LOOPDEV_MAX_DEVICES);
}
