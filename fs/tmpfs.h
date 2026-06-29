#ifndef TMPFS_H
#define TMPFS_H

#include "vfs.h"

#define TMPFS_MAX_NAME    255
#define TMPFS_INITIAL_CAP 64
#define TMPFS_DEFAULT_SIZE (4 * 1024 * 1024)

typedef struct tmpfs_node {
    uint32_t ino;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t capacity;
    uint8_t *data;
    struct tmpfs_node *parent;
    struct tmpfs_node *child;
    struct tmpfs_node *next_sibling;
    char name[TMPFS_MAX_NAME + 1];
    uint32_t nlinks;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
} tmpfs_node_t;

typedef struct tmpfs_sb_info {
    uint32_t max_size;
    uint32_t used_size;
    uint32_t inode_count;
    tmpfs_node_t *root;
} tmpfs_sb_info_t;

int32_t tmpfs_mount(superblock_t *sb, void *data);
int32_t tmpfs_init(void);

typedef struct tmpfs_stats {
    uint32_t total_bytes;
    uint32_t used_bytes;
    uint32_t free_bytes;
    uint32_t node_count;
    uint32_t file_count;
    uint32_t dir_count;
} tmpfs_stats_t;

int32_t tmpfs_get_stats(tmpfs_stats_t *out);

#endif
