#include "shmdev.h"
#include "klog.h"
#include "devfs.h"
#include "kheap.h"
#include "string.h"

static shmdev_segment_t shm_segments[SHMDEV_MAX_SEGMENTS];
static int shmdev_initialized = 0;

static int find_free_segment(void) {
    for (int i = 0; i < SHMDEV_MAX_SEGMENTS; i++) {
        if (!shm_segments[i].in_use) return i;
    }
    return -1;
}

static int find_segment_by_name(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < SHMDEV_MAX_SEGMENTS; i++) {
        if (shm_segments[i].in_use && strcmp(shm_segments[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

shmdev_segment_t *shmdev_get_segment(int id) {
    if (id < 0 || id >= SHMDEV_MAX_SEGMENTS) return NULL;
    if (!shm_segments[id].in_use) return NULL;
    return &shm_segments[id];
}

int shmdev_get_count(void) {
    int count = 0;
    for (int i = 0; i < SHMDEV_MAX_SEGMENTS; i++) {
        if (shm_segments[i].in_use) count++;
    }
    return count;
}

int shmdev_create(const char *name, uint32_t size) {
    int id;

    if (!name || !*name || size == 0) return -1;

    if (find_segment_by_name(name) >= 0) {
        klog_warn("shmdev: segment '%s' already exists", name);
        return -1;
    }

    id = find_free_segment();
    if (id < 0) {
        klog_err("shmdev: no free segments");
        return -1;
    }

    shm_segments[id].data = (uint8_t *)kmalloc(size);
    if (!shm_segments[id].data) {
        klog_err("shmdev: failed to allocate %u bytes", size);
        return -1;
    }

    memset(shm_segments[id].data, 0, size);
    strncpy(shm_segments[id].name, name, SHMDEV_MAX_NAME - 1);
    shm_segments[id].name[SHMDEV_MAX_NAME - 1] = '\0';
    shm_segments[id].size = size;
    shm_segments[id].in_use = 1;
    shm_segments[id].ref_count = 0;
    shm_segments[id].permissions = 0666;

    klog_info("shmdev: created segment '%s' (id=%d, size=%u)", name, id, size);
    return id;
}

int shmdev_open(const char *name) {
    int id;

    if (!name || !*name) return -1;

    id = find_segment_by_name(name);
    if (id < 0) {
        return -1;
    }

    shm_segments[id].ref_count++;
    return id;
}

int shmdev_read(int id, uint32_t offset, uint32_t count, void *buf) {
    shmdev_segment_t *seg;

    if (!buf || count == 0) return -1;

    seg = shmdev_get_segment(id);
    if (!seg) return -1;

    if (offset >= seg->size) return 0;
    if (offset + count > seg->size) {
        count = seg->size - offset;
    }

    memcpy(buf, seg->data + offset, count);
    return (int)count;
}

int shmdev_write(int id, uint32_t offset, uint32_t count, const void *buf) {
    shmdev_segment_t *seg;

    if (!buf || count == 0) return -1;

    seg = shmdev_get_segment(id);
    if (!seg) return -1;

    if (offset >= seg->size) return -1;
    if (offset + count > seg->size) {
        count = seg->size - offset;
    }

    memcpy(seg->data + offset, buf, count);
    return (int)count;
}

int shmdev_close(int id) {
    shmdev_segment_t *seg = shmdev_get_segment(id);
    if (!seg) return -1;

    if (seg->ref_count > 0) {
        seg->ref_count--;
    }
    return 0;
}

int shmdev_unlink(const char *name) {
    int id = find_segment_by_name(name);
    shmdev_segment_t *seg;

    if (id < 0) return -1;

    seg = &shm_segments[id];
    if (seg->ref_count > 0) {
        klog_warn("shmdev: segment '%s' still in use (ref=%u)", name, seg->ref_count);
    }

    if (seg->data) {
        kfree(seg->data);
        seg->data = NULL;
    }

    memset(seg, 0, sizeof(shmdev_segment_t));
    klog_info("shmdev: removed segment '%s'", name);
    return 0;
}

static int32_t shmdev_dev_read(file_t *file, void *buf, uint32_t count) {
    (void)file;
    (void)buf;
    (void)count;
    return -ENOSYS;
}

static int32_t shmdev_dev_write(file_t *file, const void *buf, uint32_t count) {
    (void)file;
    (void)buf;
    (void)count;
    return -ENOSYS;
}

static int32_t shmdev_dev_open(inode_t *inode, file_t *file) {
    (void)inode;
    (void)file;
    return 0;
}

static int32_t shmdev_dev_close(file_t *file) {
    (void)file;
    return 0;
}

static file_ops_t shmdev_ops = {
    .open = shmdev_dev_open,
    .read = shmdev_dev_read,
    .write = shmdev_dev_write,
    .close = shmdev_dev_close,
    .seek = NULL,
    .ioctl = NULL
};

int shmdev_init(void) {
    memset(shm_segments, 0, sizeof(shm_segments));

    if (devfs_register("shm", DEVICE_CHAR, SHMDEV_MAJOR, SHMDEV_MINOR,
                       &shmdev_ops, NULL) != 0) {
        klog_warn("shmdev: failed to register /dev/shm");
    } else {
        klog_info("shmdev: registered /dev/shm");
    }

    shmdev_initialized = 1;
    klog_info("shmdev: initialized (max %d segments)", SHMDEV_MAX_SEGMENTS);
    return 0;
}
