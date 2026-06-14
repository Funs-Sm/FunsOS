#ifndef RAMFS_H
#define RAMFS_H

#include "vfs.h"

#define RAMFS_MAX_NAME    255
#define RAMFS_INITIAL_CAP 64

/* A single in-memory node (file or directory) */
typedef struct ramfs_node {
    uint32_t ino;
    uint32_t mode;          /* FILE_MODE_DIR, FILE_MODE_REG, ... */
    uint32_t uid;
    uint32_t gid;
    uint32_t size;          /* valid bytes in data */
    uint32_t capacity;      /* allocated bytes in data */
    uint8_t *data;          /* file content (NULL for directories) */
    struct ramfs_node *parent;
    struct ramfs_node *child;       /* first child (head of list) */
    struct ramfs_node *next_sibling;
    char name[RAMFS_MAX_NAME + 1];
    uint32_t nlinks;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
} ramfs_node_t;

int32_t ramfs_mount(superblock_t *sb, void *data);
int32_t ramfs_init(void);

/* Statistics helper used by `df` */
typedef struct ramfs_stats {
    uint32_t total_bytes;     /* total capacity across all files */
    uint32_t used_bytes;      /* bytes actually holding data */
    uint32_t node_count;      /* number of inodes */
    uint32_t file_count;
    uint32_t dir_count;
} ramfs_stats_t;

int32_t ramfs_get_stats(ramfs_stats_t *out);

#endif
