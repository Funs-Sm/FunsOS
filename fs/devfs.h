#ifndef DEVFS_H
#define DEVFS_H

#include "vfs.h"

#define DEVICE_CHAR  0
#define DEVICE_BLOCK 1

#define DEV_PERM_READ  0444
#define DEV_PERM_WRITE 0222
#define DEV_PERM_RW    0666
#define DEV_PERM_ROOT_RW 0600

typedef struct devfs_device {
    char name[32];
    uint32_t type;
    uint32_t major;
    uint32_t minor;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    file_ops_t *ops;
    void *private_data;
    struct devfs_device *next;
} devfs_device_t;

int32_t devfs_init(void);
int32_t devfs_register(const char *name, uint32_t type, uint32_t major, uint32_t minor, file_ops_t *ops, void *data);
int32_t devfs_unregister(const char *name);
devfs_device_t *devfs_find(const char *name);
void devfs_create_std_devices(void);
int32_t devfs_register_with_perm(const char *name, uint32_t type, uint32_t major, uint32_t minor,
                                 file_ops_t *ops, void *data, uint32_t mode, uint32_t uid, uint32_t gid);

#endif
