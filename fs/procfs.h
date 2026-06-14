#ifndef PROCFS_H
#define PROCFS_H

#include "vfs.h"
#include "stdint.h"

#define PROCFS_MAX_PROCS 256

typedef struct {
    char name[64];
    uint32_t ino;
    uint32_t mode;
    uint32_t size;
    int32_t (*read_proc)(char *buf, uint32_t offset, uint32_t count);
} procfs_entry_t;

int32_t procfs_init(void);
int32_t procfs_mount(superblock_t *sb, void *data);
int32_t procfs_open(inode_t *inode, file_t *file);
int32_t procfs_read(file_t *file, void *buf, uint32_t count);
int32_t procfs_readdir(file_t *file, void *buf, uint32_t count);
int32_t procfs_stat(inode_t *inode, inode_t *stat);

#endif
