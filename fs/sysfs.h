#ifndef SYSFS_H
#define SYSFS_H

#include "vfs.h"
#include "stdint.h"

typedef struct {
    char name[64];
    uint32_t mode;
    int32_t (*show)(char *buf);
    int32_t (*store)(const char *buf, uint32_t count);
} sysfs_attr_t;

int32_t sysfs_init(void);
int32_t sysfs_mount(superblock_t *sb, void *data);
dentry_t *sysfs_create_dir(const char *name, dentry_t *parent);
dentry_t *sysfs_create_file(const char *name, dentry_t *parent, sysfs_attr_t *attr);
int32_t sysfs_read(file_t *file, void *buf, uint32_t count);
int32_t sysfs_write(file_t *file, const void *buf, uint32_t count);

#endif
